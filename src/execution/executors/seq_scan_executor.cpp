#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (iter_ != end_) {
    *tuple = *iter_;
    *rid = tuple->GetRID();
    ++iter_;
    
    const auto &predicate = plan_->GetPredicate();
    if (predicate != nullptr) {
      auto value = predicate->Evaluate(tuple, &plan_->GetOutputSchema());
      if (!value.GetAsBoolean()) {
        continue;
      }
    }
    
    const auto &schema = plan_->GetOutputSchema();
    std::vector<Value> values;
    for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
      values.push_back(tuple->GetValue(&schema, i));
    }
    Tuple populated(std::move(values));
    populated.SetRID(*rid);
    *tuple = populated;
    
    return true;
  }
  return false;
}

}  // namespace onebase
