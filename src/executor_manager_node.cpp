#include <memory>
#include <string>
#include <sstream>
#include <algorithm>
#include <chrono>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

#include "std_msgs/msg/empty.hpp"
#include "std_srvs/srv/trigger.hpp"

#include "executor_interfaces/msg/task_step.hpp"
#include "executor_interfaces/msg/task_result.hpp"
#include "executor_interfaces/action/execute_task.hpp"
#include "supervisor_executor_interfaces/srv/authorize_task.hpp"

using namespace std::chrono_literals;

class ExecutorManagerNode : public rclcpp::Node
{
public:
    using Empty       = std_msgs::msg::Empty;
    using Trigger     = std_srvs::srv::Trigger;
    using ExecuteTask = executor_interfaces::action::ExecuteTask;
    using TaskStep    = executor_interfaces::msg::TaskStep;
    using TaskResult  = executor_interfaces::msg::TaskResult;
    using GoalHandle  = rclcpp_action::ClientGoalHandle<ExecuteTask>;
    using AuthorizeTask = supervisor_executor_interfaces::srv::AuthorizeTask;

    ExecutorManagerNode()
    : Node("executor_manager_node")
    {
        trigger_sub_ = create_subscription<Empty>(
            "/input_trigger", 10,
            std::bind(&ExecutorManagerNode::triggerCallback,
                      this, std::placeholders::_1));

        instruction_client_ = create_client<Trigger>("/request_instruction");

        action_client_ = rclcpp_action::create_client<ExecuteTask>(
            this, "/executor/execute_task");

        authorize_client_ = create_client<AuthorizeTask>(
            "/supervisor/supervisor_executor/authorize");

        RCLCPP_INFO(get_logger(), "ExecutorManagerNode ready.");
    }

private:

    // =========================================================
    // Trigger
    // =========================================================
    void triggerCallback(const Empty::SharedPtr)
    {
        RCLCPP_INFO(get_logger(), "Trigger received.");
        requestNextInstruction();
    }

    // =========================================================
    // Request instruction
    // =========================================================
    void requestNextInstruction()
    {
        if (!instruction_client_->wait_for_service(5s)) {
            RCLCPP_ERROR(get_logger(), "Instruction service unavailable.");
            return;
        }

        instruction_client_->async_send_request(
            std::make_shared<Trigger::Request>(),
            std::bind(&ExecutorManagerNode::instructionCallback,
                      this, std::placeholders::_1));
    }

    void instructionCallback(rclcpp::Client<Trigger>::SharedFuture future)
    {
        auto response = future.get();

        if (!response->success && response->message == "blocked_by_manager") {
            RCLCPP_WARN(get_logger(),
                        "Instruction dispatch blocked by supervisor_plan; retrying shortly.");
            scheduleInstructionRetry();
            return;
        }

        if (!response->success || response->message.empty()) {
            RCLCPP_INFO(get_logger(), "No more instructions.");
            return;
        }
        if (response->message == "stop") {
            RCLCPP_INFO(get_logger(), "Stop received.");
            return;
        }

        current_task_ = parseInstruction(response->message);
        sendGoal();
    }

    void scheduleInstructionRetry()
    {
        if (instruction_retry_timer_) {
            instruction_retry_timer_->cancel();
            instruction_retry_timer_.reset();
        }

        instruction_retry_timer_ = create_wall_timer(
            200ms,
            [this]()
            {
                if (instruction_retry_timer_) {
                    instruction_retry_timer_->cancel();
                    instruction_retry_timer_.reset();
                }
                requestNextInstruction();
            });
    }

    TaskStep parseInstruction(const std::string& raw)
    {
        TaskStep step;
        step.step_id = 1;

        std::stringstream ss(raw);
        ss >> step.agent >> step.action;
        std::transform(step.action.begin(), step.action.end(),
                       step.action.begin(), ::tolower);

        std::string rest;
        std::getline(ss, rest);
        if (!rest.empty()) {
            rest.erase(0, rest.find_first_not_of(" "));
            step.args.push_back(rest);
        }

        return step;
    }

    // =========================================================
    // Send goal to worker
    // =========================================================
    void sendGoal()
    {
        if (!action_client_->wait_for_action_server(3s)) {
            RCLCPP_ERROR(get_logger(), "ExecutorWorker not available.");
            return;
        }

        ExecuteTask::Goal goal;
        goal.task = current_task_;

        auto options = rclcpp_action::Client<ExecuteTask>::SendGoalOptions();

        options.goal_response_callback =
            [this](const GoalHandle::SharedPtr& gh) {
                if (!gh) {
                    RCLCPP_ERROR(get_logger(), "Goal rejected by worker.");
                    requestNextInstruction();
                } else {
                    RCLCPP_INFO(get_logger(), "Goal accepted.");
                }
            };

        options.feedback_callback =
            [this](GoalHandle::SharedPtr,
                   const std::shared_ptr<const ExecuteTask::Feedback> fb)
            {
                RCLCPP_INFO(get_logger(), "Feedback: %s",
                            fb->feedback.message.c_str());
            };

        options.result_callback =
            [this](const GoalHandle::WrappedResult& result) {
                onResult(result);
            };

        action_client_->async_send_goal(goal, options);
    }

    // =========================================================
    // Result
    // =========================================================
    void onResult(const GoalHandle::WrappedResult& result)
	{
		if (!result.result) {
		    RCLCPP_ERROR(get_logger(), "Null result.");
		    return;
		}

		const auto& task_result = result.result->result;

		// SUCCESS o FAILED — en ambos casos el supervisor decide qué sigue
		RCLCPP_INFO(get_logger(), "Task %s: %s",
		            task_result.status == TaskResult::SUCCESS ? "SUCCESS" : "FAILED",
		            task_result.message.c_str());

		requestAuthorize(task_result.message);
	}

    // =========================================================
    // Authorize — pide aprobación al supervisor
    // =========================================================
    void requestAuthorize(const std::string& worker_result)
    {
        if (!authorize_client_->wait_for_service(2s)) {
            RCLCPP_ERROR(get_logger(), "Authorize service unavailable — skipping.");
            requestNextInstruction();
            return;
        }

        auto req = std::make_shared<AuthorizeTask::Request>();
        req->action         = current_task_.action;
        req->argument       = current_task_.args.empty() ? "" : current_task_.args[0];
        req->worker_result  = worker_result;
        req->context_message = worker_result;

        authorize_client_->async_send_request(
            req,
            [this](rclcpp::Client<AuthorizeTask>::SharedFuture future)
            {
                try {
                    auto res = future.get();
                    if (res->approved) {
                        RCLCPP_INFO(get_logger(), "Authorized: %s", res->message.c_str());
                        requestNextInstruction();
                    } else {
                        RCLCPP_WARN(get_logger(), "Rejected: %s", res->message.c_str());
                        requestNextInstruction();
                    }
                } catch (const std::exception& e) {
                    RCLCPP_ERROR(get_logger(), "Authorize exception: %s", e.what());
                    requestNextInstruction();
                }
            });
    }

    // =========================================================
    // Members
    // =========================================================
    rclcpp::Subscription<Empty>::SharedPtr          trigger_sub_;
    rclcpp::Client<Trigger>::SharedPtr              instruction_client_;
    rclcpp_action::Client<ExecuteTask>::SharedPtr   action_client_;
    rclcpp::Client<AuthorizeTask>::SharedPtr        authorize_client_;
    rclcpp::TimerBase::SharedPtr                    instruction_retry_timer_;

    TaskStep current_task_;
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ExecutorManagerNode>());
    rclcpp::shutdown();
    return 0;
}
