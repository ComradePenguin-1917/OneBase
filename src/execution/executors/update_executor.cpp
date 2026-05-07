#include "onebase/execution/executors/update_executor.h"
#include "onebase/common/exception.h"
#include "onebase/type/value.h"

namespace onebase {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void UpdateExecutor::Init() {
  child_executor_->Init();
  has_updated_ = false;
}

auto UpdateExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_updated_) {
    return false;
  }
  has_updated_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  const auto &schema = table_info->schema_;

  int count = 0;
  Tuple child_tuple;
  RID child_rid;
  
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    std::vector<Value> new_values;
    for (const auto &expr : plan_->GetUpdateExpressions()) {
      new_values.push_back(expr->Evaluate(&child_tuple, &schema));
    }
    Tuple new_tuple(std::move(new_values));
    table_info->table_->UpdateTuple(child_rid, new_tuple);
    count++;
  }

  *tuple = Tuple({Value(TypeId::INTEGER, count)});
  return true;
}

}  // namespace onebase
