#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <chrono>
#include <algorithm> 
#include <cctype>

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
    service_client_ = this->create_client<Trigger>("request_instruction");

    action_client_ = rclcpp_action::create_client<ExecuteTask>(this, "execute_task");

    subscription_ = this->create_subscription<Empty>(
      "input_trigger", 10, std::bind(&TaskManagerNode::trigger_callback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "TaskManagerNode initialized. Waiting for signal on topic '/input_trigger'...");
  }

private:
  void trigger_callback(const Empty::SharedPtr msg)
  {
    (void)msg;
    RCLCPP_INFO(this->get_logger(), "Trigger received! Starting AUTOMATIC workflow...");
    
    request_next_instruction();
  }

  void request_next_instruction()
  {
    if (!service_client_->wait_for_service(1s)) {
      RCLCPP_ERROR(this->get_logger(), "Service 'request_instruction' not available. Stopping loop.");
      return;
    }

    auto request = std::make_shared<Trigger::Request>();
    RCLCPP_INFO(this->get_logger(), "Requesting NEXT instruction...");

    service_client_->async_send_request(request, 
      std::bind(&TaskManagerNode::service_response_callback, this, std::placeholders::_1));
  }

  void service_response_callback(rclcpp::Client<Trigger>::SharedFuture future)
  {
    auto response = future.get();
    if (response->success) {
      std::string raw_instruction = response->message;
      RCLCPP_INFO(this->get_logger(), "Instruction received from Service: '%s'", raw_instruction.c_str());

      if (raw_instruction == "stop" || raw_instruction.empty()) {
          RCLCPP_INFO(this->get_logger(), "Stop command received. Workflow finished.");
          return;
      }

      current_task_ = parse_instruction(raw_instruction);
      retry_count_ = 0;

      send_action_goal(current_task_);

    } else {
      RCLCPP_ERROR(this->get_logger(), "Service responded with success=false.");
    }
  }

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
  std::transform(action_part.begin(), action_part.end(), action_part.begin(),
                   [](unsigned char c){ return std::tolower(c); });
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

    /*RCLCPP_INFO(this->get_logger(), "Sending Goal -> Action: %s, Args: %s", 
      step_msg.action.c_str(), 
      (step_msg.args.empty() ? "None" : step_msg.args[0].c_str()));*/

    RCLCPP_INFO(this->get_logger(), "Sending Goal (Attempt %d/%d) -> %s", 
                retry_count_ + 1, MAX_RETRIES + 1, step_msg.action.c_str());

    auto send_goal_options = rclcpp_action::Client<ExecuteTask>::SendGoalOptions();
    
    send_goal_options.feedback_callback = 
      [this](GoalHandleExecuteTask::SharedPtr, 
             const std::shared_ptr<const ExecuteTask::Feedback> feedback) 
      {
        RCLCPP_INFO(this->get_logger(), ">> Feedback: %s", feedback->feedback.message.c_str());
      };

    send_goal_options.result_callback = 
      [this](const GoalHandleExecuteTask::WrappedResult & result) {
        
        if (result.code == rclcpp_action::ResultCode::SUCCEEDED) {
          RCLCPP_INFO(this->get_logger(), "Task completed! Requesting NEXT task...");
          
          // Succeeded: calling the next instruction
          this->request_next_instruction(); 

        } else {
          // FAIL: Retry
          if (this->retry_count_ < this->MAX_RETRIES) {
            this->retry_count_++;
            RCLCPP_WARN(this->get_logger(), "Task failed! Retrying (%d/%d)...", 
                        this->retry_count_, this->MAX_RETRIES);
            
            // Resending the saved task
            this->send_action_goal(this->current_task_);

          } else {
            // No more tries
            RCLCPP_ERROR(this->get_logger(), "Task failed after retries. Stopping workflow.");
          }
        }
      };

    action_client_->async_send_goal(goal_msg, send_goal_options);
  }

  rclcpp::Subscription<Empty>::SharedPtr subscription_; 
  rclcpp::Client<Trigger>::SharedPtr service_client_;
  rclcpp_action::Client<ExecuteTask>::SharedPtr action_client_;
  TaskStep current_task_;      // Current task saved
  int retry_count_ = 0;        // Tries counter
  const int MAX_RETRIES = 1;   // Max Tries
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TaskManagerNode>());
  rclcpp::shutdown();
  return 0;
}