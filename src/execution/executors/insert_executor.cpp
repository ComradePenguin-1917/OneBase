#include "onebase/execution/executors/insert_executor.h"
#include "onebase/common/exception.h"
#include "onebase/type/value.h"

namespace onebase {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  child_executor_->Init();
  has_inserted_ = false;
}

auto InsertExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_inserted_) {
    return false;
  }
  has_inserted_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int count = 0;
  Tuple child_tuple;
  RID child_rid;
  
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    auto new_rid = table_info->table_->InsertTuple(child_tuple);
    if (new_rid.has_value()) {
      for (auto *index_info : indexes) {
        std::vector<Value> key_values;
        for (uint32_t attr : index_info->key_attrs_) {
          key_values.push_back(child_tuple.GetValue(&table_info->schema_, attr));
        }
        Tuple key_tuple(std::move(key_values));
      }
      count++;
    }
  }

  *tuple = Tuple({Value(TypeId::INTEGER, count)});
  return true;
}

}  // namespace onebase
