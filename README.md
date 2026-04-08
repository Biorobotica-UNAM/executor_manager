# Executor Manager Package

## Proyecto Biorrobótica UNAM

## Description
The **`executor_manager`** package acts as the bridge between the high-level **Supervisor** (Planner) and the low-level **Executor Worker**. It is responsible for orchestrating the execution flow: requesting instructions, parsing raw string commands into actionable tasks, and managing the task lifecycle.

This node ensures that Justina follows the plan sequentially while providing a layer of fault tolerance.

---

## Key Features
* **Instruction Orchestration**: Requests the next step in the plan from the `/request_instruction` service only when the previous task is completed.
* **String Command Parsing**: Transforms raw text instructions (e.g., "Justina goto table") into `TaskStep` messages with identified agents, actions, and arguments.
* **Failure & Retry Logic**: Includes a configurable retry mechanism (`MAX_RETRIES`) to attempt tasks again before reporting a permanent failure.
* **Deadlock Prevention**: Uses a `goal_response_callback` to detect when a task is rejected by the Worker, automatically skipping to the next instruction to keep the robot moving.

---

## Execution Workflow

1. **Trigger**: The process starts when a message is received on the `/input_trigger` topic.
2. **Fetch**: The manager calls the **Supervisor** service to get the next instruction.
3. **Parse**: The raw string is converted into a structured `ExecuteTask` action goal.
4. **Dispatch**: The goal is sent to the **Executor Worker**.
5. **Monitor**: 
    - **Success**: Requests the next instruction.
    - **Rejection/Failure**: Attempts a retry or moves to the next step depending on the error status.

---

## Handled Statuses
The manager reacts differently based on the feedback from the Worker:

| Status | Outcome |
| :--- | :--- |
| **SUCCEEDED** | Task completed. Requests next step. |
| **REJECTED** | Invalid action. Skips to next step immediately. |
| **FAILURE** | Execution failed. Triggers retry logic (up to `MAX_RETRIES`). |
| **ABORTED** | Critical failure. Stops current execution thread. |

---

## Dependencies
* `rclcpp` & `rclcpp_action`
* `std_msgs` & `std_srvs`
* `executor_interfaces` (Action and Message definitions)

---

## Usage

To launch the Manager node:
```bash
ros2 run executor_manager executor_manager_node