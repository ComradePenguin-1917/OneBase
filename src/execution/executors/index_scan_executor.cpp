#include "onebase/execution/executors/index_scan_executor.h"
#include "onebase/common/exception.h"
#include "onebase/execution/expressions/constant_value_expression.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *index_info = catalog->GetIndex(plan_->GetIndexOid());
  
  rids_.clear();
  cursor_ = 0;
  
  if (index_info == nullptr || !index_info->SupportsPointLookup()) {
    return;
  }
  
  const auto &lookup_key = plan_->GetLookupKey();
  if (lookup_key != nullptr) {
    auto *const_val = dynamic_cast<ConstantValueExpression *>(lookup_key.get());
    if (const_val != nullptr) {
      int32_t key = const_val->GetValue().GetAsInteger();
      auto *found_rids = index_info->LookupInteger(key);
      if (found_rids != nullptr) {
        rids_ = *found_rids;
      }
    }
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  
  while (cursor_ < rids_.size()) {
    RID current_rid = rids_[cursor_++];
    Tuple child_tuple = table_info->table_->GetTuple(current_rid);
    
    const auto &predicate = plan_->GetPredicate();
    if (predicate != nullptr) {
      auto value = predicate->Evaluate(&child_tuple, &plan_->GetOutputSchema());
      if (!value.GetAsBoolean()) {
        continue;
      }
    }
    
    *rid = current_rid;
    const auto &schema = plan_->GetOutputSchema();
    std::vector<Value> values;
    for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
      values.push_back(child_tuple.GetValue(&schema, i));
    }
    Tuple populated(std::move(values));
    populated.SetRID(current_rid);
    *tuple = populated;
    return true;
  }
  return false;
}

}  // namespace onebase
