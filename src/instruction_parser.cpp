#include <memory>
#include <string>
#include <sstream>
#include <iostream>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

class InstructionParser : public rclcpp::Node
{
public:
  InstructionParser()
  : Node("instruction_parser_node")
  {
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "instruccion", 10, std::bind(&InstructionParser::topic_callback, this, std::placeholders::_1));

    publisher_ = this->create_publisher<std_msgs::msg::String>("robot_command", 10);

    RCLCPP_INFO(this->get_logger(), "Node started, waiting for instruccions");
  }

private:
  void topic_callback(const std_msgs::msg::String::SharedPtr msg) const
  {
    std::stringstream ss(msg->data);
    std::string temp, command, target;

    ss >> temp; 
    ss >> temp; 
    ss >> temp; 

    ss >> command;

    std::getline(ss, target);

    const size_t first = target.find_first_not_of(' ');
    if (std::string::npos != first) {
        target = target.substr(first);
    }

    auto message = std_msgs::msg::String();
    message.data = command + "|" + target; 
    
    RCLCPP_INFO(this->get_logger(), "Received: %s -> Sent: [%s | %s]", 
                msg->data.c_str(), command.c_str(), target.c_str());
    
    publisher_->publish(message);
  }

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<InstructionParser>());
  rclcpp::shutdown();
  return 0;
}