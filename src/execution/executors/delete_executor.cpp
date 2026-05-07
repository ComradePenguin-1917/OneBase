#include "onebase/execution/executors/delete_executor.h"
#include "onebase/common/exception.h"
#include "onebase/type/value.h"

namespace onebase {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  child_executor_->Init();
  has_deleted_ = false;
}

auto DeleteExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_deleted_) {
    return false;
  }
  has_deleted_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int count = 0;
  Tuple child_tuple;
  RID child_rid;
  
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    table_info->table_->DeleteTuple(child_rid);
    for (auto *index_info : indexes) {
      std::vector<Value> key_values;
      for (uint32_t attr : index_info->key_attrs_) {
        key_values.push_back(child_tuple.GetValue(&table_info->schema_, attr));
      }
      Tuple key_tuple(std::move(key_values));
    }
    count++;
  }

  *tuple = Tuple({Value(TypeId::INTEGER, count)});
  return true;
}

}  // namespace onebase
