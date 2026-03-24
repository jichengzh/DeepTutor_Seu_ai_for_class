#include "module_2_planning/dynamic_obstacle_predictor.hpp"

#include <cmath>
#include <algorithm>

namespace module_2_planning {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: 4x4 matrix operations (row-major, in-place)
// ─────────────────────────────────────────────────────────────────────────────

/// Add two 4x4 matrices: C = A + B
static void mat4Add(
  const std::array<double, 16>& A,
  const std::array<double, 16>& B,
  std::array<double, 16>& C)
{
  for (int i = 0; i < 16; ++i) C[i] = A[i] + B[i];
}

/// Multiply two 4x4 matrices: C = A * B
static void mat4Mul(
  const std::array<double, 16>& A,
  const std::array<double, 16>& B,
  std::array<double, 16>& C)
{
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      double s = 0.0;
      for (int k = 0; k < 4; ++k) {
        s += A[i * 4 + k] * B[k * 4 + j];
      }
      C[i * 4 + j] = s;
    }
  }
}

/// Transpose a 4x4 matrix: B = A^T
static void mat4Trans(
  const std::array<double, 16>& A,
  std::array<double, 16>& B)
{
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      B[j * 4 + i] = A[i * 4 + j];
}

/// Multiply 4x4 matrix by 4-vector: y = A * x
static std::array<double, 4> mat4Vec(
  const std::array<double, 16>& A,
  const std::array<double, 4>& x)
{
  std::array<double, 4> y{};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      y[i] += A[i * 4 + j] * x[j];
    }
  }
  return y;
}

// ─────────────────────────────────────────────────────────────────────────────
// DynamicObstaclePredictor — constructor
// ─────────────────────────────────────────────────────────────────────────────
DynamicObstaclePredictor::DynamicObstaclePredictor(
  const rclcpp::NodeOptions& options)
: rclcpp::Node("dynamic_obstacle_predictor", options)
{
  // Declare parameters
  declare_parameter("prediction_steps",  rclcpp::ParameterValue(10));
  declare_parameter("prediction_dt",     rclcpp::ParameterValue(0.1));
  declare_parameter("grid_resolution",   rclcpp::ParameterValue(0.1));
  declare_parameter("grid_half_width",   rclcpp::ParameterValue(10.0));
  declare_parameter("fixed_frame",       rclcpp::ParameterValue(std::string("map")));

  get_parameter("prediction_steps",  prediction_steps_);
  get_parameter("prediction_dt",     prediction_dt_);
  get_parameter("grid_resolution",   grid_resolution_);
  get_parameter("grid_half_width",   grid_half_width_);
  get_parameter("fixed_frame",       fixed_frame_);

  // Subscribers / Publishers
  obstacle_sub_ = create_subscription<visualization_msgs::msg::MarkerArray>(
    "/tracked_obstacles", rclcpp::QoS(10),
    std::bind(&DynamicObstaclePredictor::obstacleCallback, this,
              std::placeholders::_1));

  occupancy_pub_ = create_publisher<nav_msgs::msg::OccupancyGrid>(
    "/predicted_occupancy", rclcpp::QoS(10));

  // Periodic publish timer (10 Hz)
  publish_timer_ = create_wall_timer(
    std::chrono::milliseconds(100),
    std::bind(&DynamicObstaclePredictor::publishOccupancy, this));

  RCLCPP_INFO(get_logger(),
    "DynamicObstaclePredictor ready: steps=%d dt=%.2f grid_res=%.2f",
    prediction_steps_, prediction_dt_, grid_resolution_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kalman prediction step: x = F*x,  P = F*P*F' + Q
// ─────────────────────────────────────────────────────────────────────────────
void DynamicObstaclePredictor::propagate(KalmanState& state, double dt) const {
  // State transition matrix F (constant velocity)
  //   [1 0 dt  0]
  //   [0 1  0 dt]
  //   [0 0  1  0]
  //   [0 0  0  1]
  std::array<double, 16> F = {
    1, 0, dt, 0,
    0, 1, 0, dt,
    0, 0, 1,  0,
    0, 0, 0,  1
  };

  // x = F * x
  state.x = mat4Vec(F, state.x);

  // P = F * P * F'
  std::array<double, 16> FP{}, FPFt{}, Ft{};
  mat4Mul(F, state.P, FP);
  mat4Trans(F, Ft);
  mat4Mul(FP, Ft, FPFt);

  // Q (process noise)
  double qp = state.q_pos * dt * dt;
  double qv = state.q_vel * dt;
  std::array<double, 16> Q = {
    qp, 0,  0,  0,
    0, qp,  0,  0,
    0,  0, qv,  0,
    0,  0,  0, qv
  };
  mat4Add(FPFt, Q, state.P);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kalman update step: K = P*H'*(H*P*H'+R)^-1,  x += K*(z - H*x),  P = (I-KH)*P
// We use H = [1 0 0 0; 0 1 0 0]  (observe position only)
// ─────────────────────────────────────────────────────────────────────────────
void DynamicObstaclePredictor::update(
  KalmanState& state, double meas_x, double meas_y) const
{
  // Innovation z - H*x  (2-vector: position only)
  double inn_x = meas_x - state.x[0];
  double inn_y = meas_y - state.x[1];

  // S = H*P*H' + R  (2x2, H picks rows 0 and 1 of P)
  double r = state.r_pos;
  double S00 = state.P[0]  + r;   // P[0,0] + R
  double S01 = state.P[1];         // P[0,1]
  double S10 = state.P[4];         // P[1,0]
  double S11 = state.P[5]  + r;   // P[1,1] + R

  // S^-1
  double det = S00 * S11 - S01 * S10;
  if (std::fabs(det) < 1e-12) return;  // degenerate
  double Si00 =  S11 / det;
  double Si01 = -S01 / det;
  double Si10 = -S10 / det;
  double Si11 =  S00 / det;

  // K = P*H' * S^-1  (4x2 * 2x2 = 4x2)
  // P*H' picks columns 0 and 1 of P (since H has identity in first two rows)
  // K[:,0] = P[:,0]*Si00 + P[:,1]*Si10
  // K[:,1] = P[:,0]*Si01 + P[:,1]*Si11
  std::array<double, 8> K{};  // 4 rows x 2 cols
  for (int i = 0; i < 4; ++i) {
    double Ph0 = state.P[i * 4 + 0];
    double Ph1 = state.P[i * 4 + 1];
    K[i * 2 + 0] = Ph0 * Si00 + Ph1 * Si10;
    K[i * 2 + 1] = Ph0 * Si01 + Ph1 * Si11;
  }

  // x = x + K * innovation
  for (int i = 0; i < 4; ++i) {
    state.x[i] += K[i * 2 + 0] * inn_x + K[i * 2 + 1] * inn_y;
  }

  // P = (I - K*H) * P
  // K*H is 4x4; K*H[i,j] = K[i,0]*H[0,j] + K[i,1]*H[1,j]
  // Since H selects rows 0 and 1, K*H[:,0] = K[:,0], K*H[:,1] = K[:,1], rest 0
  std::array<double, 16> KH{};
  for (int i = 0; i < 4; ++i) {
    KH[i * 4 + 0] = K[i * 2 + 0];
    KH[i * 4 + 1] = K[i * 2 + 1];
  }
  // (I - KH)
  std::array<double, 16> IKH{};
  for (int i = 0; i < 4; ++i) IKH[i * 4 + i] = 1.0;
  for (int i = 0; i < 16; ++i) IKH[i] -= KH[i];

  std::array<double, 16> newP{};
  mat4Mul(IKH, state.P, newP);
  state.P = newP;
}

// ─────────────────────────────────────────────────────────────────────────────
// obstacleCallback
// ─────────────────────────────────────────────────────────────────────────────
void DynamicObstaclePredictor::obstacleCallback(
  const visualization_msgs::msg::MarkerArray::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(state_mutex_);

  for (const auto& marker : msg->markers) {
    int id = marker.id;
    double mx = marker.pose.position.x;
    double my = marker.pose.position.y;

    auto& state = obstacle_states_[id];
    if (!state.initialized) {
      state.x = {mx, my, 0.0, 0.0};
      state.initialized = true;
      state.last_update  = now();
    } else {
      double dt = (now() - state.last_update).seconds();
      if (dt > 0.0 && dt < 5.0) {
        propagate(state, dt);
        update(state, mx, my);
      } else {
        // Re-initialise if gap is too large
        state.x = {mx, my, 0.0, 0.0};
      }
      state.last_update = now();
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// predict — return N-step-ahead positions for all tracked obstacles
// ─────────────────────────────────────────────────────────────────────────────
std::unordered_map<int, std::vector<PredictedPosition>>
DynamicObstaclePredictor::predict(int steps, double dt) const
{
  std::lock_guard<std::mutex> lock(state_mutex_);
  std::unordered_map<int, std::vector<PredictedPosition>> result;

  for (const auto& [id, state] : obstacle_states_) {
    if (!state.initialized) continue;

    KalmanState tmp = state;
    std::vector<PredictedPosition> preds;
    preds.reserve(static_cast<size_t>(steps));

    for (int s = 0; s < steps; ++s) {
      propagate(tmp, dt);
      PredictedPosition pp;
      pp.x = tmp.x[0];
      pp.y = tmp.x[1];
      // Approximate 1-sigma radius from covariance trace of position block
      pp.uncertainty_radius = std::sqrt(tmp.P[0] + tmp.P[5]);
      preds.push_back(pp);
    }
    result[id] = std::move(preds);
  }
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// publishOccupancy
// ─────────────────────────────────────────────────────────────────────────────
void DynamicObstaclePredictor::publishOccupancy()
{
  auto predictions = predict(prediction_steps_, prediction_dt_);

  // Build occupancy grid centred at origin (or robot pose — simplified here)
  int cells = static_cast<int>(2.0 * grid_half_width_ / grid_resolution_);
  nav_msgs::msg::OccupancyGrid grid;
  grid.header.stamp    = now();
  grid.header.frame_id = fixed_frame_;
  grid.info.resolution = static_cast<float>(grid_resolution_);
  grid.info.width      = static_cast<unsigned int>(cells);
  grid.info.height     = static_cast<unsigned int>(cells);
  grid.info.origin.position.x = -grid_half_width_;
  grid.info.origin.position.y = -grid_half_width_;
  grid.info.origin.orientation.w = 1.0;
  grid.data.assign(static_cast<size_t>(cells * cells), 0);

  // Mark all predicted positions with a Gaussian footprint
  for (const auto& [id, preds] : predictions) {
    for (size_t step = 0; step < preds.size(); ++step) {
      const auto& pp = preds[step];
      double sigma = std::max(pp.uncertainty_radius, grid_resolution_);
      int cx = static_cast<int>((pp.x + grid_half_width_) / grid_resolution_);
      int cy = static_cast<int>((pp.y + grid_half_width_) / grid_resolution_);
      int radius_cells = static_cast<int>(sigma * 3.0 / grid_resolution_) + 1;

      for (int dy = -radius_cells; dy <= radius_cells; ++dy) {
        for (int dx = -radius_cells; dx <= radius_cells; ++dx) {
          int gx = cx + dx;
          int gy = cy + dy;
          if (gx < 0 || gx >= cells || gy < 0 || gy >= cells) continue;

          double dist = std::sqrt(
            static_cast<double>(dx * dx + dy * dy)) * grid_resolution_;
          double prob = std::exp(-(dist * dist) / (2.0 * sigma * sigma));
          // Decay with prediction horizon
          prob *= 1.0 - static_cast<double>(step) / (preds.size() + 1);

          int& cell = reinterpret_cast<int&>(
            grid.data[static_cast<size_t>(gy * cells + gx)]);
          int new_val = static_cast<int>(prob * 100.0);
          if (new_val > cell) cell = new_val;
        }
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    latest_grid_ = grid;
  }
  occupancy_pub_->publish(grid);
}

nav_msgs::msg::OccupancyGrid DynamicObstaclePredictor::getOccupancyGrid() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return latest_grid_;
}

}  // namespace module_2_planning

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<module_2_planning::DynamicObstaclePredictor>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
