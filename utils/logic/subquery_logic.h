#ifndef UTILS_LOGIC_SUBQUERY_LOGIC_H
#define UTILS_LOGIC_SUBQUERY_LOGIC_H

#include "logic_ast.h"

namespace service {
struct QueryExecuteContext;
struct QueryExecuteResult;
struct SelectRowsResult;
class QueryExecutor;
} // namespace service

namespace logic {

struct CorrelatedBinding {
    QString name;
    QString value;
    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
    bool isNull = false;
};

struct CorrelationBindings {
    QList<CorrelatedBinding> items;
};

class ISubqueryExecutor {
public:
    virtual ~ISubqueryExecutor() = default;

    virtual service::QueryExecuteResult executeSelectSql(const QString &sql,
                                                         const service::QueryExecuteContext &context) = 0;

    virtual service::QueryExecuteResult executeCorrelatedSelect(const QString &sql,
                                                                const CorrelationBindings &bindings,
                                                                const service::QueryExecuteContext &context) = 0;
};

class LogicSubqueryExecutorAdapter : public ISubqueryExecutor {
public:
    explicit LogicSubqueryExecutorAdapter(service::QueryExecutor *executor);

    service::QueryExecuteResult executeSelectSql(const QString &sql,
                                const service::QueryExecuteContext &context) override;

    service::QueryExecuteResult executeCorrelatedSelect(const QString &sql,
                                                        const CorrelationBindings &bindings,
                                    const service::QueryExecuteContext &context) override;

private:
    service::QueryExecutor *m_executor = nullptr;
};

CorrelationBindings buildCorrelationBindings(const LogicRowContext &outerRowContext,
                                             const QStringList &referencedOuterNames);

LogicEvalResult evaluateExistsSubqueryNode(const LogicNode &node,
                                           const LogicRowContext &rowContext,
                                           const LogicEvalContext &evalContext);

LogicEvalResult evaluateInSubqueryNode(const LogicNode &node,
                                       const LogicRowContext &rowContext,
                                       const LogicEvalContext &evalContext);

LogicEvalResult evaluateQuantifiedSubqueryNode(const LogicNode &node,
                                               const LogicRowContext &rowContext,
                                               const LogicEvalContext &evalContext);

QList<setdef::SetValue> normalizeSelectResultToSet(const service::SelectRowsResult &result);

} // namespace logic

#endif // UTILS_LOGIC_SUBQUERY_LOGIC_H