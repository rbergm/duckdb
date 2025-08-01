//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/table/column_data.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/storage/statistics/base_statistics.hpp"
#include "duckdb/storage/data_pointer.hpp"
#include "duckdb/storage/table/persistent_table_data.hpp"
#include "duckdb/storage/statistics/segment_statistics.hpp"
#include "duckdb/storage/table/segment_tree.hpp"
#include "duckdb/storage/table/column_segment_tree.hpp"
#include "duckdb/common/mutex.hpp"
#include "duckdb/common/enums/scan_vector_type.hpp"
#include "duckdb/common/serializer/serialization_traits.hpp"
#include "duckdb/common/atomic_ptr.hpp"

namespace duckdb {
class ColumnData;
class ColumnSegment;
class DatabaseInstance;
class RowGroup;
class RowGroupWriter;
class StorageManager;
class TableDataWriter;
class TableStorageInfo;
struct DataTableInfo;
struct PrefetchState;
struct RowGroupWriteInfo;
struct TableScanOptions;
struct TransactionData;
struct PersistentColumnData;

using column_segment_vector_t = vector<SegmentNode<ColumnSegment>>;

struct ColumnCheckpointInfo {
	ColumnCheckpointInfo(RowGroupWriteInfo &info, idx_t column_idx) : info(info), column_idx(column_idx) {
	}

	RowGroupWriteInfo &info;
	idx_t column_idx;

public:
	CompressionType GetCompressionType();
};

class ColumnData {
	friend class ColumnDataCheckpointer;

public:
	ColumnData(BlockManager &block_manager, DataTableInfo &info, idx_t column_index, idx_t start_row, LogicalType type,
	           optional_ptr<ColumnData> parent);
	virtual ~ColumnData();

	//! The start row
	idx_t start;
	//! The count of the column data
	atomic<idx_t> count;
	//! The block manager
	BlockManager &block_manager;
	//! Table info for the column
	DataTableInfo &info;
	//! The column index of the column, either within the parent table or within the parent
	idx_t column_index;
	//! The type of the column
	LogicalType type;

public:
	virtual FilterPropagateResult CheckZonemap(ColumnScanState &state, TableFilter &filter);

	BlockManager &GetBlockManager() {
		return block_manager;
	}
	DatabaseInstance &GetDatabase() const;
	DataTableInfo &GetTableInfo() const;
	StorageManager &GetStorageManager() const;
	virtual idx_t GetMaxEntry();

	idx_t GetAllocationSize() const {
		return allocation_size;
	}
	optional_ptr<const CompressionFunction> GetCompressionFunction() const {
		return compression.get();
	}

	bool HasParent() const {
		return parent != nullptr;
	}
	const ColumnData &Parent() const {
		D_ASSERT(HasParent());
		return *parent;
	}

	virtual void SetStart(idx_t new_start);
	//! The root type of the column
	const LogicalType &RootType() const;
	//! Whether or not the column has any updates
	bool HasUpdates() const;
	bool HasChanges(idx_t start_row, idx_t end_row) const;
	//! Whether or not we can scan an entire vector
	virtual ScanVectorType GetVectorScanType(ColumnScanState &state, idx_t scan_count, Vector &result);

	//! Initialize prefetch state with required I/O data for the next N rows
	virtual void InitializePrefetch(PrefetchState &prefetch_state, ColumnScanState &scan_state, idx_t rows);
	//! Initialize a scan of the column
	virtual void InitializeScan(ColumnScanState &state);
	//! Initialize a scan starting at the specified offset
	virtual void InitializeScanWithOffset(ColumnScanState &state, idx_t row_idx);
	//! Scan the next vector from the column
	idx_t Scan(TransactionData transaction, idx_t vector_index, ColumnScanState &state, Vector &result);
	idx_t ScanCommitted(idx_t vector_index, ColumnScanState &state, Vector &result, bool allow_updates);
	virtual idx_t Scan(TransactionData transaction, idx_t vector_index, ColumnScanState &state, Vector &result,
	                   idx_t scan_count);
	virtual idx_t ScanCommitted(idx_t vector_index, ColumnScanState &state, Vector &result, bool allow_updates,
	                            idx_t scan_count);

	virtual void ScanCommittedRange(idx_t row_group_start, idx_t offset_in_row_group, idx_t count, Vector &result);
	virtual idx_t ScanCount(ColumnScanState &state, Vector &result, idx_t count, idx_t result_offset = 0);

	//! Select
	virtual void Filter(TransactionData transaction, idx_t vector_index, ColumnScanState &state, Vector &result,
	                    SelectionVector &sel, idx_t &count, const TableFilter &filter, TableFilterState &filter_state);
	virtual void Select(TransactionData transaction, idx_t vector_index, ColumnScanState &state, Vector &result,
	                    SelectionVector &sel, idx_t count);
	virtual void SelectCommitted(idx_t vector_index, ColumnScanState &state, Vector &result, SelectionVector &sel,
	                             idx_t count, bool allow_updates);

	//! Skip the scan forward by "count" rows
	virtual void Skip(ColumnScanState &state, idx_t count = STANDARD_VECTOR_SIZE);

	//! Initialize an appending phase for this column
	virtual void InitializeAppend(ColumnAppendState &state);
	//! Append a vector of type [type] to the end of the column
	virtual void Append(BaseStatistics &stats, ColumnAppendState &state, Vector &vector, idx_t count);
	//! Append a vector of type [type] to the end of the column
	void Append(ColumnAppendState &state, Vector &vector, idx_t count);
	virtual void AppendData(BaseStatistics &stats, ColumnAppendState &state, UnifiedVectorFormat &vdata, idx_t count);
	//! Revert a set of appends to the ColumnData
	virtual void RevertAppend(row_t start_row);

	//! Fetch the vector from the column data that belongs to this specific row
	virtual idx_t Fetch(ColumnScanState &state, row_t row_id, Vector &result);
	//! Fetch a specific row id and append it to the vector
	virtual void FetchRow(TransactionData transaction, ColumnFetchState &state, row_t row_id, Vector &result,
	                      idx_t result_idx);

	virtual void Update(TransactionData transaction, idx_t column_index, Vector &update_vector, row_t *row_ids,
	                    idx_t update_count);
	virtual void UpdateColumn(TransactionData transaction, const vector<column_t> &column_path, Vector &update_vector,
	                          row_t *row_ids, idx_t update_count, idx_t depth);
	virtual unique_ptr<BaseStatistics> GetUpdateStatistics();

	virtual void CommitDropColumn();

	virtual unique_ptr<ColumnCheckpointState> CreateCheckpointState(RowGroup &row_group,
	                                                                PartialBlockManager &partial_block_manager);
	virtual unique_ptr<ColumnCheckpointState> Checkpoint(RowGroup &row_group, ColumnCheckpointInfo &info);

	virtual void CheckpointScan(ColumnSegment &segment, ColumnScanState &state, idx_t row_group_start, idx_t count,
	                            Vector &scan_vector);

	virtual bool IsPersistent();
	vector<DataPointer> GetDataPointers();

	virtual PersistentColumnData Serialize();
	void InitializeColumn(PersistentColumnData &column_data);
	virtual void InitializeColumn(PersistentColumnData &column_data, BaseStatistics &target_stats);
	static shared_ptr<ColumnData> Deserialize(BlockManager &block_manager, DataTableInfo &info, idx_t column_index,
	                                          idx_t start_row, ReadStream &source, const LogicalType &type);

	virtual void GetColumnSegmentInfo(idx_t row_group_index, vector<idx_t> col_path, vector<ColumnSegmentInfo> &result);
	virtual void Verify(RowGroup &parent);

	FilterPropagateResult CheckZonemap(TableFilter &filter);

	static shared_ptr<ColumnData> CreateColumn(BlockManager &block_manager, DataTableInfo &info, idx_t column_index,
	                                           idx_t start_row, const LogicalType &type,
	                                           optional_ptr<ColumnData> parent = nullptr);
	static unique_ptr<ColumnData> CreateColumnUnique(BlockManager &block_manager, DataTableInfo &info,
	                                                 idx_t column_index, idx_t start_row, const LogicalType &type,
	                                                 optional_ptr<ColumnData> parent = nullptr);

	void MergeStatistics(const BaseStatistics &other);
	void MergeIntoStatistics(BaseStatistics &other);
	unique_ptr<BaseStatistics> GetStatistics();

protected:
	//! Append a transient segment
	void AppendTransientSegment(SegmentLock &l, idx_t start_row);
	void AppendSegment(SegmentLock &l, unique_ptr<ColumnSegment> segment);

	void BeginScanVectorInternal(ColumnScanState &state);
	//! Scans a base vector from the column
	idx_t ScanVector(ColumnScanState &state, Vector &result, idx_t remaining, ScanVectorType scan_type,
	                 idx_t result_offset = 0);
	//! Scans a vector from the column merged with any potential updates
	idx_t ScanVector(TransactionData transaction, idx_t vector_index, ColumnScanState &state, Vector &result,
	                 idx_t target_scan, ScanVectorType scan_type, ScanVectorMode mode);
	idx_t ScanVector(TransactionData transaction, idx_t vector_index, ColumnScanState &state, Vector &result,
	                 idx_t target_scan, ScanVectorMode mode);
	void SelectVector(ColumnScanState &state, Vector &result, idx_t target_count, const SelectionVector &sel,
	                  idx_t sel_count);
	void FilterVector(ColumnScanState &state, Vector &result, idx_t target_count, SelectionVector &sel,
	                  idx_t &sel_count, const TableFilter &filter, TableFilterState &filter_state);

	void ClearUpdates();
	void FetchUpdates(TransactionData transaction, idx_t vector_index, Vector &result, idx_t scan_count,
	                  bool allow_updates, bool scan_committed);
	void FetchUpdateRow(TransactionData transaction, row_t row_id, Vector &result, idx_t result_idx);
	void UpdateInternal(TransactionData transaction, idx_t column_index, Vector &update_vector, row_t *row_ids,
	                    idx_t update_count, Vector &base_vector);

	idx_t GetVectorCount(idx_t vector_index) const;

private:
	void UpdateCompressionFunction(SegmentLock &l, const CompressionFunction &function);

protected:
	//! The segments holding the data of this column segment
	ColumnSegmentTree data;
	//! The lock for the updates
	mutable mutex update_lock;
	//! The updates for this column segment
	unique_ptr<UpdateSegment> updates;
	//! The lock for the stats
	mutable mutex stats_lock;
	//! The stats of the root segment
	unique_ptr<SegmentStatistics> stats;
	//! Total transient allocation size
	atomic<idx_t> allocation_size;

private:
	//! The parent column (if any)
	optional_ptr<ColumnData> parent;
	//!	The compression function used by the ColumnData
	//! This is empty if the segments have mixed compression or the ColumnData is empty
	atomic_ptr<const CompressionFunction> compression;
};

struct PersistentColumnData {
	explicit PersistentColumnData(PhysicalType physical_type);
	PersistentColumnData(PhysicalType physical_type, vector<DataPointer> pointers);
	// disable copy constructors
	PersistentColumnData(const PersistentColumnData &other) = delete;
	PersistentColumnData &operator=(const PersistentColumnData &) = delete;
	//! enable move constructors
	PersistentColumnData(PersistentColumnData &&other) noexcept = default;
	PersistentColumnData &operator=(PersistentColumnData &&) = default;
	~PersistentColumnData();

	PhysicalType physical_type;
	vector<DataPointer> pointers;
	vector<PersistentColumnData> child_columns;
	bool has_updates = false;

	void Serialize(Serializer &serializer) const;
	static PersistentColumnData Deserialize(Deserializer &deserializer);
	void DeserializeField(Deserializer &deserializer, field_id_t field_idx, const char *field_name,
	                      const LogicalType &type);
	bool HasUpdates() const;
};

struct PersistentRowGroupData {
	explicit PersistentRowGroupData(vector<LogicalType> types);
	PersistentRowGroupData() = default;
	// disable copy constructors
	PersistentRowGroupData(const PersistentRowGroupData &other) = delete;
	PersistentRowGroupData &operator=(const PersistentRowGroupData &) = delete;
	//! enable move constructors
	PersistentRowGroupData(PersistentRowGroupData &&other) noexcept = default;
	PersistentRowGroupData &operator=(PersistentRowGroupData &&) = default;
	~PersistentRowGroupData() = default;

	vector<LogicalType> types;
	vector<PersistentColumnData> column_data;
	idx_t start;
	idx_t count;

	void Serialize(Serializer &serializer) const;
	static PersistentRowGroupData Deserialize(Deserializer &deserializer);
	bool HasUpdates() const;
};

struct PersistentCollectionData {
	PersistentCollectionData() = default;
	// disable copy constructors
	PersistentCollectionData(const PersistentCollectionData &other) = delete;
	PersistentCollectionData &operator=(const PersistentCollectionData &) = delete;
	//! enable move constructors
	PersistentCollectionData(PersistentCollectionData &&other) noexcept = default;
	PersistentCollectionData &operator=(PersistentCollectionData &&) = default;
	~PersistentCollectionData() = default;

	vector<PersistentRowGroupData> row_group_data;

	void Serialize(Serializer &serializer) const;
	static PersistentCollectionData Deserialize(Deserializer &deserializer);
	bool HasUpdates() const;
};

} // namespace duckdb
