#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <chrono>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

#include "std_srvs/srv/trigger.hpp"
#include "executor_interfaces/msg/task_step.hpp"
#include "executor_interfaces/action/execute_task.hpp"

using namespace std::chrono_literals;

class TaskManagerNode : public rclcpp::Node
{
public:
  using Trigger = std_srvs::srv::Trigger;
  using TaskStep = executor_interfaces::msg::TaskStep;
  using ExecuteTask = executor_interfaces::action::ExecuteTask;
  using GoalHandleExecuteTask = rclcpp_action::ClientGoalHandle<ExecuteTask>;

  TaskManagerNode()
  : Node("task_manager_node")
  {
    // 1. Service Client (To ASK for the instruction)
    service_client_ = this->create_client<Trigger>("request_instruccion");

    // 2. Action Client (To EXECUTE the task)
    action_client_ = rclcpp_action::create_client<ExecuteTask>(this, "execute_task");

    RCLCPP_INFO(this->get_logger(), "TaskManagerNode initialized.");

    // We use a one-shot timer to start the workflow after 1 second,
    // ensuring the node is fully initialized before making requests.
    timer_ = this->create_wall_timer(
      1s, std::bind(&TaskManagerNode::start_workflow, this));
  }

private:
  // --- STEP 1: REQUEST INSTRUCTION ---
  void start_workflow()
  {
    timer_->cancel(); // Execute only once
    
    // Wait for the service to be available
    if (!service_client_->wait_for_service(2s)) {
      RCLCPP_ERROR(this->get_logger(), "Service 'request_instruccion' not available.");
      return;
    }

    auto request = std::make_shared<Trigger::Request>();
    RCLCPP_INFO(this->get_logger(), "Calling service 'request_instruccion'...");

    // Async call
    auto future_result = service_client_->async_send_request(request, 
      std::bind(&TaskManagerNode::service_response_callback, this, std::placeholders::_1));
  }

  // --- STEP 2: RECEIVE AND PARSE ---
  void service_response_callback(rclcpp::Client<Trigger>::SharedFuture future)
  {
    auto response = future.get();
    if (response->success) {
      std::string raw_instruction = response->message; // Example: "goto living room"
      RCLCPP_INFO(this->get_logger(), "Instruction received: '%s'", raw_instruction.c_str());

      // Parse the string into a TaskStep object
      TaskStep task_step = parse_instruction(raw_instruction);

      // --- STEP 3: SEND TO ACTION SERVER ---
      send_action_goal(task_step);

    } else {
      RCLCPP_ERROR(this->get_logger(), "Service responded with success=false.");
    }
  }

  TaskStep parse_instruction(const std::string & raw_str)
  {
    TaskStep step;
    step.step_id = 1;         // You could use a global counter here
    step.agent = "Justina";   // Assumed default

    std::stringstream ss(raw_str);
    std::string action_part, args_part;

    // Extract the first word as 'action' (e.g., "goto")
    ss >> action_part;
    step.action = action_part;

    // Extract the rest of the line as 'args' (e.g., "living room")
    std::getline(ss, args_part);

    // Trim leading spaces from arguments
    const size_t first = args_part.find_first_not_of(' ');
    if (std::string::npos != first) {
      args_part = args_part.substr(first);
    }

    // If arguments exist, add them to the list
    if (!args_part.empty()) {
      step.args.push_back(args_part);
    }

    return step;
  }

  // --- STEP 3 (DETAIL): SEND GOAL ---
  void send_action_goal(const TaskStep & step_msg)
  {
    if (!action_client_->wait_for_action_server(2s)) {
      RCLCPP_ERROR(this->get_logger(), "Action Server 'execute_task' not available.");
      return;
    }

    auto goal_msg = ExecuteTask::Goal();
    
    // Assigning the TaskStep object to the 'task' field in the Goal
    goal_msg.task = step_msg;

    RCLCPP_INFO(this->get_logger(), "Sending Goal -> Action: %s, Args: %s", 
      step_msg.action.c_str(), 
      (step_msg.args.empty() ? "None" : step_msg.args[0].c_str()));

    auto send_goal_options = rclcpp_action::Client<ExecuteTask>::SendGoalOptions();
    
    // Callback to check if the task finished successfully
    send_goal_options.result_callback = 
      [this](const GoalHandleExecuteTask::WrappedResult & result) {
        if (result.code == rclcpp_action::ResultCode::SUCCEEDED) {
          RCLCPP_INFO(this->get_logger(), "Task completed successfully!");
        } else {
          RCLCPP_WARN(this->get_logger(), "Task failed or was canceled.");
        }
      };

    action_client_->async_send_goal(goal_msg, send_goal_options);
  }

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Client<Trigger>::SharedPtr service_client_;
  rclcpp_action::Client<ExecuteTask>::SharedPtr action_client_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TaskManagerNode>());
  rclcpp::shutdown();
  return 0;
}