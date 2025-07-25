#include "duckdb/common/types/vector_buffer.hpp"

#include "duckdb/common/assert.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/storage/buffer/buffer_handle.hpp"

namespace duckdb {

buffer_ptr<VectorBuffer> VectorBuffer::CreateStandardVector(PhysicalType type, idx_t capacity) {
	return make_buffer<VectorBuffer>(capacity * GetTypeIdSize(type));
}

buffer_ptr<VectorBuffer> VectorBuffer::CreateConstantVector(PhysicalType type) {
	return make_buffer<VectorBuffer>(GetTypeIdSize(type));
}

buffer_ptr<VectorBuffer> VectorBuffer::CreateConstantVector(const LogicalType &type) {
	return VectorBuffer::CreateConstantVector(type.InternalType());
}

buffer_ptr<VectorBuffer> VectorBuffer::CreateStandardVector(const LogicalType &type, idx_t capacity) {
	return VectorBuffer::CreateStandardVector(type.InternalType(), capacity);
}

VectorStringBuffer::VectorStringBuffer() : VectorBuffer(VectorBufferType::STRING_BUFFER) {
}

VectorStringBuffer::VectorStringBuffer(Allocator &allocator)
    : VectorBuffer(VectorBufferType::STRING_BUFFER), heap(allocator) {
}

VectorStringBuffer::VectorStringBuffer(VectorBufferType type) : VectorBuffer(type) {
}

VectorFSSTStringBuffer::VectorFSSTStringBuffer() : VectorStringBuffer(VectorBufferType::FSST_BUFFER) {
}

VectorStructBuffer::VectorStructBuffer() : VectorBuffer(VectorBufferType::STRUCT_BUFFER) {
}

VectorStructBuffer::VectorStructBuffer(const LogicalType &type, idx_t capacity)
    : VectorBuffer(VectorBufferType::STRUCT_BUFFER) {
	auto &child_types = StructType::GetChildTypes(type);
	for (auto &child_type : child_types) {
		auto vector = make_uniq<Vector>(child_type.second, capacity);
		children.push_back(std::move(vector));
	}
}

VectorStructBuffer::VectorStructBuffer(Vector &other, const SelectionVector &sel, idx_t count)
    : VectorBuffer(VectorBufferType::STRUCT_BUFFER) {
	auto &other_vector = StructVector::GetEntries(other);
	for (auto &child_vector : other_vector) {
		auto vector = make_uniq<Vector>(*child_vector, sel, count);
		children.push_back(std::move(vector));
	}
}

VectorStructBuffer::~VectorStructBuffer() {
}

VectorListBuffer::VectorListBuffer(unique_ptr<Vector> vector, idx_t initial_capacity)
    : VectorBuffer(VectorBufferType::LIST_BUFFER), child(std::move(vector)), capacity(initial_capacity) {
}

VectorListBuffer::VectorListBuffer(const LogicalType &list_type, idx_t initial_capacity)
    : VectorBuffer(VectorBufferType::LIST_BUFFER),
      child(make_uniq<Vector>(ListType::GetChildType(list_type), initial_capacity)), capacity(initial_capacity) {
}

void VectorListBuffer::Reserve(idx_t to_reserve) {
	if (to_reserve > capacity) {
		if (to_reserve > DConstants::MAX_VECTOR_SIZE) {
			// overflow: throw an exception
			throw OutOfRangeException("Cannot resize vector to %d rows: maximum allowed vector size is %s", to_reserve,
			                          StringUtil::BytesToHumanReadableString(DConstants::MAX_VECTOR_SIZE));
		}
		idx_t new_capacity = NextPowerOfTwo(to_reserve);
		D_ASSERT(new_capacity >= to_reserve);
		child->Resize(capacity, new_capacity);
		capacity = new_capacity;
	}
}

void VectorListBuffer::Append(const Vector &to_append, idx_t to_append_size, idx_t source_offset) {
	Reserve(size + to_append_size - source_offset);
	VectorOperations::Copy(to_append, *child, to_append_size, source_offset, size);
	size += to_append_size - source_offset;
}

void VectorListBuffer::Append(const Vector &to_append, const SelectionVector &sel, idx_t to_append_size,
                              idx_t source_offset) {
	Reserve(size + to_append_size - source_offset);
	VectorOperations::Copy(to_append, *child, sel, to_append_size, source_offset, size);
	size += to_append_size - source_offset;
}

void VectorListBuffer::PushBack(const Value &insert) {
	while (size + 1 > capacity) {
		child->Resize(capacity, capacity * 2);
		capacity *= 2;
	}
	child->SetValue(size++, insert);
}

void VectorListBuffer::SetCapacity(idx_t new_capacity) {
	this->capacity = new_capacity;
}

void VectorListBuffer::SetSize(idx_t new_size) {
	this->size = new_size;
}

VectorListBuffer::~VectorListBuffer() {
}

VectorArrayBuffer::VectorArrayBuffer(unique_ptr<Vector> child_vector, idx_t array_size, idx_t initial_capacity)
    : VectorBuffer(VectorBufferType::ARRAY_BUFFER), child(std::move(child_vector)), array_size(array_size),
      size(initial_capacity) {
	D_ASSERT(array_size != 0);
}

VectorArrayBuffer::VectorArrayBuffer(const LogicalType &array, idx_t initial)
    : VectorBuffer(VectorBufferType::ARRAY_BUFFER),
      child(make_uniq<Vector>(ArrayType::GetChildType(array), initial * ArrayType::GetSize(array))),
      array_size(ArrayType::GetSize(array)), size(initial) {
	// initialize the child array with (array_size * size) ^
	D_ASSERT(!ArrayType::IsAnySize(array));
}

VectorArrayBuffer::~VectorArrayBuffer() {
}

Vector &VectorArrayBuffer::GetChild() {
	return *child;
}

idx_t VectorArrayBuffer::GetArraySize() {
	return array_size;
}

idx_t VectorArrayBuffer::GetChildSize() {
	return size * array_size;
}

ManagedVectorBuffer::ManagedVectorBuffer(BufferHandle handle)
    : VectorBuffer(VectorBufferType::MANAGED_BUFFER), handle(std::move(handle)) {
}

ManagedVectorBuffer::~ManagedVectorBuffer() {
}

} // namespace duckdb
