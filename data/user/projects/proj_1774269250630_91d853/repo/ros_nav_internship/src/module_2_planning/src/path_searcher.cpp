#include "module_2_planning/path_searcher.hpp"

#include <rclcpp/rclcpp.hpp>

namespace module_2_planning {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
PathSearcher::PathSearcher(const rclcpp::NodeOptions& options)
: rclcpp_lifecycle::LifecycleNode("path_searcher", options)
{
  declare_parameter("planner_name",  rclcpp::ParameterValue(std::string("HybridAStarPlanner")));
  declare_parameter("global_frame",  rclcpp::ParameterValue(std::string("map")));
}

// ─────────────────────────────────────────────────────────────────────────────
// on_configure
// ─────────────────────────────────────────────────────────────────────────────
rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
PathSearcher::on_configure(const rclcpp_lifecycle::State& /*state*/)
{
  get_parameter("planner_name", planner_name_);
  get_parameter("global_frame", global_frame_);

  // TF
  tf_buffer_   = std::make_shared<tf2_ros::Buffer>(get_clock());
  tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

  // Costmap
  costmap_ros_ = std::make_shared<nav2_costmap_2d::Costmap2DROS>(
    "path_searcher_costmap",
    std::string{get_namespace()},
    "path_searcher_costmap",
    get_parameter("use_sim_time").as_bool());
  costmap_ros_->configure();

  // Planner
  planner_ = std::make_shared<HybridAStarPlanner>();
  planner_->configure(
    shared_from_this(), planner_name_, tf_buffer_, costmap_ros_);

  // Subscribers
  start_sub_ = create_subscription<geometry_msgs::msg::PoseStamped>(
    "/start_pose", rclcpp::QoS(1),
    std::bind(&PathSearcher::startCallback, this, std::placeholders::_1));

  goal_sub_ = create_subscription<geometry_msgs::msg::PoseStamped>(
    "/goal_pose", rclcpp::QoS(1),
    std::bind(&PathSearcher::goalCallback, this, std::placeholders::_1));

  // Publisher
  path_pub_ = create_publisher<nav_msgs::msg::Path>("/raw_path", rclcpp::QoS(10));

  RCLCPP_INFO(get_logger(), "PathSearcher configured.");
  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// on_activate
// ─────────────────────────────────────────────────────────────────────────────
rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
PathSearcher::on_activate(const rclcpp_lifecycle::State& /*state*/)
{
  costmap_ros_->activate();
  planner_->activate();
  path_pub_->on_activate();
  RCLCPP_INFO(get_logger(), "PathSearcher activated.");
  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// on_deactivate
// ─────────────────────────────────────────────────────────────────────────────
rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
PathSearcher::on_deactivate(const rclcpp_lifecycle::State& /*state*/)
{
  planner_->deactivate();
  costmap_ros_->deactivate();
  path_pub_->on_deactivate();
  RCLCPP_INFO(get_logger(), "PathSearcher deactivated.");
  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// on_cleanup
// ─────────────────────────────────────────────────────────────────────────────
rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
PathSearcher::on_cleanup(const rclcpp_lifecycle::State& /*state*/)
{
  planner_->cleanup();
  costmap_ros_->cleanup();
  planner_.reset();
  costmap_ros_.reset();
  tf_buffer_.reset();
  tf_listener_.reset();
  start_sub_.reset();
  goal_sub_.reset();
  path_pub_.reset();
  RCLCPP_INFO(get_logger(), "PathSearcher cleaned up.");
  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// on_shutdown
// ─────────────────────────────────────────────────────────────────────────────
rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
PathSearcher::on_shutdown(const rclcpp_lifecycle::State& /*state*/)
{
  RCLCPP_INFO(get_logger(), "PathSearcher shutting down.");
  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────
void PathSearcher::startCallback(
  const geometry_msgs::msg::PoseStamped::SharedPtr msg)
{
  current_start_ = msg;
  tryPlan();
}

void PathSearcher::goalCallback(
  const geometry_msgs::msg::PoseStamped::SharedPtr msg)
{
  current_goal_ = msg;
  tryPlan();
}

// ─────────────────────────────────────────────────────────────────────────────
// tryPlan
// ─────────────────────────────────────────────────────────────────────────────
void PathSearcher::tryPlan()
{
  if (!current_start_ || !current_goal_) return;
  if (!path_pub_->is_activated()) {
    RCLCPP_WARN(get_logger(), "PathSearcher not active yet — ignoring plan request");
    return;
  }

  RCLCPP_INFO(get_logger(),
    "Planning from (%.2f, %.2f) to (%.2f, %.2f)",
    current_start_->pose.position.x, current_start_->pose.position.y,
    current_goal_->pose.position.x,  current_goal_->pose.position.y);

  try {
    nav_msgs::msg::Path path = planner_->createPlan(*current_start_, *current_goal_);
    if (!path.poses.empty()) {
      path_pub_->publish(path);
      RCLCPP_INFO(get_logger(), "Published raw_path with %zu poses", path.poses.size());
    } else {
      RCLCPP_WARN(get_logger(), "Planner returned empty path");
    }
  } catch (const std::exception& e) {
    RCLCPP_ERROR(get_logger(), "Planning failed: %s", e.what());
  }
}

}  // namespace module_2_planning

// ── main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<module_2_planning::PathSearcher>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
