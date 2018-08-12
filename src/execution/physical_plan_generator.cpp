
#include "execution/physical_plan_generator.hpp"

#include "execution/operator/physical_filter.hpp"
#include "execution/operator/physical_hash_aggregate.hpp"
#include "execution/operator/physical_insert.hpp"
#include "execution/operator/physical_limit.hpp"
#include "execution/operator/physical_order.hpp"
#include "execution/operator/physical_projection.hpp"
#include "execution/operator/physical_table_scan.hpp"
#include "execution/operator/physical_copy.hpp"
#include "execution/operator/physical_list.hpp"
#include "parser/expression/expression_list.hpp"

#include "planner/operator/logical_aggregate.hpp"
#include "planner/operator/logical_distinct.hpp"
#include "planner/operator/logical_filter.hpp"
#include "planner/operator/logical_get.hpp"
#include "planner/operator/logical_insert.hpp"
#include "planner/operator/logical_copy.hpp"
#include "planner/operator/logical_limit.hpp"
#include "planner/operator/logical_order.hpp"
#include "planner/operator/logical_projection.hpp"
#include "planner/operator/logical_list.hpp"

#include "storage/storage_manager.hpp"

using namespace duckdb;
using namespace std;

bool PhysicalPlanGenerator::CreatePlan(unique_ptr<LogicalOperator> logical,
                                       unique_ptr<BindContext> context) {
	this->context = move(context);
	this->success = false;
	try {
		logical->Accept(this);
		if (!this->plan) {
			throw Exception("Unknown error in physical plan generation");
		}
		this->success = true;
	} catch (Exception ex) {
		this->message = ex.GetMessage();
	} catch (...) {
		this->message = "UNHANDLED EXCEPTION TYPE THROWN IN PLANNER!";
	}
	return this->success;
}

void PhysicalPlanGenerator::Visit(LogicalAggregate &op) {
	LogicalOperatorVisitor::Visit(op);

	if (op.groups.size() == 0) {
		// no groups
		if (!plan) {
			// and no FROM clause, use a dummy aggregate
			auto groupby =
			    make_unique<PhysicalHashAggregate>(move(op.select_list));
			this->plan = move(groupby);
		} else {
			// but there is a FROM clause
			// special case: aggregate entire columns together
			auto groupby =
			    make_unique<PhysicalHashAggregate>(move(op.select_list));
			groupby->children.push_back(move(plan));
			this->plan = move(groupby);
		}
	} else {
		// groups! create a GROUP BY aggregator
		if (!plan) {
			throw Exception("Cannot have GROUP BY without FROM clause!");
		}

		auto groupby = make_unique<PhysicalHashAggregate>(move(op.select_list),
		                                                  move(op.groups));
		groupby->children.push_back(move(plan));
		this->plan = move(groupby);
	}
}

void PhysicalPlanGenerator::Visit(LogicalDistinct &op) {
	LogicalOperatorVisitor::Visit(op);

	throw NotImplementedException("distinct clause");
}

void PhysicalPlanGenerator::Visit(LogicalFilter &op) {
	LogicalOperatorVisitor::Visit(op);

	if (!plan) {
		throw Exception("Filter cannot be the first node of a plan!");
	}

	auto filter = make_unique<PhysicalFilter>(move(op.expressions));
	filter->children.push_back(move(plan));
	this->plan = move(filter);
}

void PhysicalPlanGenerator::Visit(LogicalGet &op) {
	LogicalOperatorVisitor::Visit(op);

	if (!op.table) {
		// dummy GET operation, ignore it
		return;
	}

	std::vector<size_t> column_ids;
	// look in the context for this table which columns are required
	for (auto &bound_column : context->bound_columns[op.alias]) {
		column_ids.push_back(op.table->name_map[bound_column]);
	}

	auto scan = make_unique<PhysicalTableScan>(op.table->storage, column_ids);
	if (plan) {
		throw Exception("Scan has to be the first node of a plan!");
	}
	this->plan = move(scan);
}

void PhysicalPlanGenerator::Visit(LogicalLimit &op) {
	LogicalOperatorVisitor::Visit(op);

	auto limit = make_unique<PhysicalLimit>(op.limit, op.offset);
	if (!plan) {
		throw Exception("Limit cannot be the first node of a plan!");
	}
	limit->children.push_back(move(plan));
	this->plan = move(limit);
}

void PhysicalPlanGenerator::Visit(LogicalOrder &op) {
	LogicalOperatorVisitor::Visit(op);

	if (!plan) {
		throw Exception("Order cannot be the first node of a plan!");
	}

	auto order = make_unique<PhysicalOrder>(move(op.description));
	order->children.push_back(move(plan));
	this->plan = move(order);
}

void PhysicalPlanGenerator::Visit(LogicalProjection &op) {
	LogicalOperatorVisitor::Visit(op);

	auto projection = make_unique<PhysicalProjection>(move(op.select_list));
	if (plan) {
		projection->children.push_back(move(plan));
	}
	this->plan = move(projection);
}

void PhysicalPlanGenerator::Visit(LogicalInsert &op) {
	LogicalOperatorVisitor::Visit(op);

	auto insertion = make_unique<PhysicalInsert>(op.table, move(op.value_list));
	if (plan) {
		throw Exception("Insert should be root node");
	}
	this->plan = move(insertion);
}

void PhysicalPlanGenerator::Visit(SubqueryExpression &expr) {
	auto old_plan = move(plan);
	auto old_context = move(context);
	CreatePlan(move(expr.op), move(expr.context));
	expr.plan = move(plan);
	plan = move(old_plan);
	context = move(old_context);
}

void PhysicalPlanGenerator::Visit(LogicalCopy &op) {
	LogicalOperatorVisitor::Visit(op);

	auto copy = make_unique<PhysicalCopy>(op.table,move(op.file_path),move(op.is_from),move(op.delimiter),move(op.quote),move(op.escape));
	if (plan) {
		throw Exception("Copy should be root node");
	}
	this->plan = move(copy);
}