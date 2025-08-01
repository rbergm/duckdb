//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/storage_manager.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/helper.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb/storage/data_table.hpp"
#include "duckdb/storage/table_io_manager.hpp"
#include "duckdb/storage/write_ahead_log.hpp"
#include "duckdb/storage/database_size.hpp"
#include "duckdb/common/enums/checkpoint_type.hpp"
#include "duckdb/storage/storage_options.hpp"

namespace duckdb {
class BlockManager;
class Catalog;
class CheckpointWriter;
class DatabaseInstance;
class TransactionManager;
class TableCatalogEntry;
struct PersistentCollectionData;

class StorageCommitState {
public:
	// Destruction of this object, without prior call to FlushCommit,
	// will roll back the committed changes.
	virtual ~StorageCommitState() {
	}

	//! Revert the commit
	virtual void RevertCommit() = 0;
	// Make the commit persistent
	virtual void FlushCommit() = 0;

	virtual void AddRowGroupData(DataTable &table, idx_t start_index, idx_t count,
	                             unique_ptr<PersistentCollectionData> row_group_data) = 0;
	virtual optional_ptr<PersistentCollectionData> GetRowGroupData(DataTable &table, idx_t start_index,
	                                                               idx_t &count) = 0;
	virtual bool HasRowGroupData() {
		return false;
	}
};

struct CheckpointOptions {
	CheckpointOptions()
	    : wal_action(CheckpointWALAction::DONT_DELETE_WAL), action(CheckpointAction::CHECKPOINT_IF_REQUIRED),
	      type(CheckpointType::FULL_CHECKPOINT) {
	}

	CheckpointWALAction wal_action;
	CheckpointAction action;
	CheckpointType type;
};

//! StorageManager is responsible for managing the physical storage of a persistent database.
class StorageManager {
public:
	StorageManager(AttachedDatabase &db, string path, bool read_only);
	virtual ~StorageManager();

public:
	static StorageManager &Get(AttachedDatabase &db);
	static StorageManager &Get(Catalog &catalog);

	//! Initialize a database or load an existing database from the database file path. The block_alloc_size is
	//! either set, or invalid. If invalid, then DuckDB defaults to the default_block_alloc_size (DBConfig),
	//! or the file's block allocation size, if it is an existing database.
	void Initialize(QueryContext context, StorageOptions &options);

	DatabaseInstance &GetDatabase();
	AttachedDatabase &GetAttached() {
		return db;
	}

	//! Gets the size of the WAL, or zero, if there is no WAL.
	idx_t GetWALSize();
	//! Gets the WAL of the StorageManager, or nullptr, if there is no WAL.
	optional_ptr<WriteAheadLog> GetWAL();
	//! Deletes the WAL file, and resets the unique pointer.
	void ResetWAL();

	//! Returns the database file path
	string GetDBPath() const {
		return path;
	}
	bool IsLoaded() const {
		return load_complete;
	}
	//! The path to the WAL, derived from the database file path
	string GetWALPath();
	bool InMemory();

	virtual bool AutomaticCheckpoint(idx_t estimated_wal_bytes) = 0;
	virtual unique_ptr<StorageCommitState> GenStorageCommitState(WriteAheadLog &wal) = 0;
	virtual bool IsCheckpointClean(MetaBlockPointer checkpoint_id) = 0;
	virtual void CreateCheckpoint(QueryContext context, CheckpointOptions options = CheckpointOptions()) = 0;
	virtual DatabaseSize GetDatabaseSize() = 0;
	virtual vector<MetadataBlockInfo> GetMetadataInfo() = 0;
	virtual shared_ptr<TableIOManager> GetTableIOManager(BoundCreateTableInfo *info) = 0;
	virtual BlockManager &GetBlockManager() = 0;

	void SetStorageVersion(idx_t version) {
		storage_version = version;
	}
	bool HasStorageVersion() const {
		return storage_version.IsValid();
	}
	idx_t GetStorageVersion() const {
		D_ASSERT(HasStorageVersion());
		return storage_version.GetIndex();
	}

protected:
	virtual void LoadDatabase(QueryContext context, StorageOptions &options) = 0;

protected:
	//! The attached database managed by this storage manager.
	AttachedDatabase &db;
	//! The path of the database
	string path;
	//! The WriteAheadLog of the storage manager
	unique_ptr<WriteAheadLog> wal;
	//! Whether or not the database is opened in read-only mode
	bool read_only;
	//! When loading a database, we do not yet set the wal-field. Therefore, GetWriteAheadLog must
	//! return nullptr when loading a database
	bool load_complete = false;
	//! The serialization compatibility version when reading and writing from this database
	optional_idx storage_version;

public:
	template <class TARGET>
	TARGET &Cast() {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<TARGET &>(*this);
	}
	template <class TARGET>
	const TARGET &Cast() const {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<const TARGET &>(*this);
	}
};

//! Stores the database in a single file.
class SingleFileStorageManager : public StorageManager {
public:
	SingleFileStorageManager() = delete;
	SingleFileStorageManager(AttachedDatabase &db, string path, bool read_only);

	//! The BlockManager to read from and write to blocks (meta data and data).
	unique_ptr<BlockManager> block_manager;
	//! The table I/O manager.
	unique_ptr<TableIOManager> table_io_manager;

public:
	bool AutomaticCheckpoint(idx_t estimated_wal_bytes) override;
	unique_ptr<StorageCommitState> GenStorageCommitState(WriteAheadLog &wal) override;
	bool IsCheckpointClean(MetaBlockPointer checkpoint_id) override;
	void CreateCheckpoint(QueryContext context, CheckpointOptions options) override;
	DatabaseSize GetDatabaseSize() override;
	vector<MetadataBlockInfo> GetMetadataInfo() override;
	shared_ptr<TableIOManager> GetTableIOManager(BoundCreateTableInfo *info) override;
	BlockManager &GetBlockManager() override;

protected:
	void LoadDatabase(QueryContext context, StorageOptions &options) override;
};
} // namespace duckdb
