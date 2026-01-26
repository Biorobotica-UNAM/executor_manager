#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <chrono>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

#include "std_msgs/msg/empty.hpp" 
#include "std_srvs/srv/trigger.hpp"
#include "executor_interfaces/msg/task_step.hpp"
#include "executor_interfaces/action/execute_task.hpp"

using namespace std::chrono_literals;

class TaskManagerNode : public rclcpp::Node
{
public:
  using Empty = std_msgs::msg::Empty;
  using Trigger = std_srvs::srv::Trigger;
  using TaskStep = executor_interfaces::msg::TaskStep;
  using ExecuteTask = executor_interfaces::action::ExecuteTask;
  using GoalHandleExecuteTask = rclcpp_action::ClientGoalHandle<ExecuteTask>;

  TaskManagerNode()
  : Node("task_manager_node")
  {
    service_client_ = this->create_client<Trigger>("request_instruccion");

    action_client_ = rclcpp_action::create_client<ExecuteTask>(this, "execute_task");

    subscription_ = this->create_subscription<Empty>(
      "input_trigger", 10, std::bind(&TaskManagerNode::trigger_callback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "TaskManagerNode initialized. Waiting for signal on topic '/input_trigger'...");
  }

private:
  void trigger_callback(const Empty::SharedPtr msg)
  {
    (void)msg;
    RCLCPP_INFO(this->get_logger(), "Trigger received! Starting workflow...");

    if (!service_client_->wait_for_service(1s)) {
      RCLCPP_ERROR(this->get_logger(), "Service 'request_instruccion' is not available. Try again later.");
      return;
    }

    auto request = std::make_shared<Trigger::Request>();
    RCLCPP_INFO(this->get_logger(), "Calling service 'request_instruccion'...");

    service_client_->async_send_request(request, 
      std::bind(&TaskManagerNode::service_response_callback, this, std::placeholders::_1));
  }

  void service_response_callback(rclcpp::Client<Trigger>::SharedFuture future)
  {
    auto response = future.get();
    if (response->success) {
      std::string raw_instruction = response->message;
      RCLCPP_INFO(this->get_logger(), "Instruction received from Service: '%s'", raw_instruction.c_str());

      TaskStep task_step = parse_instruction(raw_instruction);

      send_action_goal(task_step);

    } else {
      RCLCPP_ERROR(this->get_logger(), "Service responded with success=false.");
    }
  }

  /*TaskStep parse_instruction(const std::string & raw_str)
  {
    TaskStep step;
    step.step_id = 1;        
    step.agent = "Justina";   

    std::stringstream ss(raw_str);
    std::string action_part, args_part;

    ss >> action_part;
    step.action = action_part;

    std::getline(ss, args_part);

    const size_t first = args_part.find_first_not_of(' ');
    if (std::string::npos != first) {
      args_part = args_part.substr(first);
    }

    if (!args_part.empty()) {
      step.args.push_back(args_part);
    }

    return step;
  }*/

  TaskStep parse_instruction(const std::string & raw_str)
{
  TaskStep step;
  step.step_id = 1;

  std::stringstream ss(raw_str);

  std::string agent_part, action_part;
  if (!(ss >> agent_part >> action_part)) {
    RCLCPP_WARN(this->get_logger(),
      "Invalid instruction format. Expected: '<agent> <action> ...' Got: '%s'",
      raw_str.c_str());
    step.agent = "Justina";
    step.action = "";
    return step;
  }

  step.agent = agent_part;
  step.action = action_part;

  // Guardar el resto como un solo string "tal cual" (sin cambiar living_room)
  std::string args_joined;
  std::string token;
  while (ss >> token) {
    if (!args_joined.empty()) args_joined += " ";
    args_joined += token;
  }

  if (!args_joined.empty()) {
    step.args.push_back(args_joined);
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
    goal_msg.task = step_msg;

    RCLCPP_INFO(this->get_logger(), "Sending Goal -> Action: %s, Args: %s", 
      step_msg.action.c_str(), 
      (step_msg.args.empty() ? "None" : step_msg.args[0].c_str()));

    auto send_goal_options = rclcpp_action::Client<ExecuteTask>::SendGoalOptions();
    
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

  rclcpp::Subscription<Empty>::SharedPtr subscription_; 
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