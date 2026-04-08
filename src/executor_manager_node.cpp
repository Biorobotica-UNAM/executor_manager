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

using namespace std::chrono_literals;

class ExecutorManagerNode : public rclcpp::Node
{
public:
  using Empty = std_msgs::msg::Empty;
  using Trigger = std_srvs::srv::Trigger;

  using ExecuteTask = executor_interfaces::action::ExecuteTask;
  using TaskStep = executor_interfaces::msg::TaskStep;
  using TaskResult = executor_interfaces::msg::TaskResult;

  using GoalHandle = rclcpp_action::ClientGoalHandle<ExecuteTask>;

  ExecutorManagerNode()
  : Node("executor_manager_node")
  {
    trigger_sub_ = create_subscription<Empty>(
      "/input_trigger", 10,
      std::bind(&ExecutorManagerNode::triggerCallback,
                this, std::placeholders::_1));

    instruction_client_ =
      create_client<Trigger>("/request_instruction");

    action_client_ =
      rclcpp_action::create_client<ExecuteTask>(
        this, "/executor/execute_task");

    RCLCPP_INFO(get_logger(), "ExecutorManagerNode ready.");
  }

private:

  /* ===============================
     Trigger callback
     =============================== */

  void triggerCallback(const Empty::SharedPtr)
  {
    RCLCPP_INFO(get_logger(), "Trigger received.");
    requestNextInstruction();
  }

  /* ===============================
     Request instruction from supervisor_plan
     =============================== */

  void requestNextInstruction()
  {
    if (!instruction_client_->wait_for_service(5s))
    {
      RCLCPP_ERROR(get_logger(),
                   "Instruction service unavailable.");
      return;
    }

    auto req = std::make_shared<Trigger::Request>();

    instruction_client_->async_send_request(
      req,
      std::bind(&ExecutorManagerNode::instructionCallback,
                this, std::placeholders::_1));
  }

  void instructionCallback(
    rclcpp::Client<Trigger>::SharedFuture future)
  {
    auto response = future.get();

    if (!response->success || response->message.empty())
    {
      RCLCPP_INFO(get_logger(),
                  "No more instructions from supervisor_plan.");
      return;
    }

    if (response->message == "stop")
    {
      RCLCPP_INFO(get_logger(), "Stop received.");
      return;
    }

    current_task_ = parseInstruction(response->message);
    retry_count_ = 0;

    sendGoal();
  }

  TaskStep parseInstruction(const std::string & raw)
  {
    TaskStep step;
    step.step_id = 1;

    std::stringstream ss(raw);

    ss >> step.agent >> step.action;

    std::transform(step.action.begin(),
                   step.action.end(),
                   step.action.begin(),
                   ::tolower);

    std::string rest;
    std::getline(ss, rest);

    if (!rest.empty())
    {
      rest.erase(0, rest.find_first_not_of(" "));
      step.args.push_back(rest);
    }

    return step;
  }

  /* ===============================
     Send goal to executor_worker
     =============================== */

  void sendGoal()
  {
    if (!action_client_->wait_for_action_server(3s))
    {
      RCLCPP_ERROR(get_logger(),
                   "ExecutorWorker not available.");
      return;
    }

    ExecuteTask::Goal goal;
    goal.task = current_task_;

    auto options =
      rclcpp_action::Client<ExecuteTask>::SendGoalOptions();

    /* -------- Feedback -------- */

    options.feedback_callback =
      [this](GoalHandle::SharedPtr,
             const std::shared_ptr<const ExecuteTask::Feedback> feedback)
      {
        RCLCPP_INFO(get_logger(),
                    "Feedback | step: %u progress: %.2f msg: %s",
                    feedback->feedback.step_id,
                    feedback->feedback.progress,
                    feedback->feedback.message.c_str());
      };

    /* -------- Result -------- */

    options.result_callback =
      [this](const GoalHandle::WrappedResult & result)
      {
        if (!result.result)
        {
          RCLCPP_ERROR(get_logger(),
                       "Received null result.");
          return;
        }

        const auto & task_result = result.result->result;

        if (result.code == rclcpp_action::ResultCode::SUCCEEDED &&
            task_result.status == TaskResult::SUCCESS)
        {
          RCLCPP_INFO(get_logger(),
                      "Task SUCCESS | msg: %s",
                      task_result.message.c_str());

          requestNextInstruction();
        }
        else
        {
          RCLCPP_WARN(get_logger(),
                      "Task failed | status: %u msg: %s",
                      task_result.status,
                      task_result.message.c_str());

          handleFailure(task_result.status);
        }
      };

      options.goal_response_callback = 
      [this](const GoalHandle::SharedPtr & goal_handle) {
        if (!goal_handle) {
          RCLCPP_ERROR(get_logger(), "Goal was rejected by worker!");
          // AQUÍ ES DONDE PASAS A LA SIGUIENTE PARA QUE NO SE TRABE
          this->requestNextInstruction(); 
        } else {
          RCLCPP_INFO(get_logger(), "Goal accepted :)");
        }
      };

    action_client_->async_send_goal(goal, options);
  }

  /* ===============================
     Failure handling logic
     =============================== */

  void handleFailure(uint8_t status)
  {
    if (status == TaskResult::ABORTED)
    {
      RCLCPP_ERROR(get_logger(),
                   "Task aborted. Not retrying.");
      return;
    }

    if (retry_count_ < MAX_RETRIES)
    {
      retry_count_++;
      RCLCPP_WARN(get_logger(),
                  "Retrying task (%d/%d)...",
                  retry_count_, MAX_RETRIES);

      sendGoal();
    }
    else
    {
      RCLCPP_ERROR(get_logger(),
                   "Task failed permanently after retries.");
    }
  }

  /* ===============================
     Members
     =============================== */

  rclcpp::Subscription<Empty>::SharedPtr trigger_sub_;
  rclcpp::Client<Trigger>::SharedPtr instruction_client_;
  rclcpp_action::Client<ExecuteTask>::SharedPtr action_client_;

  TaskStep current_task_;
  int retry_count_ = 0;
  const int MAX_RETRIES = 1;
};

/* ===============================
   Main
   =============================== */

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ExecutorManagerNode>());
  rclcpp::shutdown();
  return 0;
}

