//
// Created by victor on 9/22/20.
//

#include "nbveplanner/camera_model.h"

void Plane::setFromPoints(const Point &p1, const Point &p2, const Point &p3) {
  Point p1p2 = p2 - p1;
  Point p1p3 = p3 - p1;

  normal_ = (p1p2.cross(p1p3)).normalized();
  distance_ = normal_.dot(p1);
}

void Plane::setFromDistanceNormal(const Point &normal, double distance) {
  normal_ = normal;
  distance_ = distance;
}

bool Plane::isPointCorrectSide(const Point &point) const {
  VLOG(5) << "Plane: normal: " << normal_.transpose()
          << " distance: " << distance_ << " point: " << point.transpose();
  VLOG(5) << "Distance: " << point.dot(normal_) + distance_;
  return point.dot(normal_) >= distance_;
}

void CameraModel::setIntrinsicsFromFoV(double horizontal_fov_deg,
                                       double vertical_fov_deg,
                                       double min_distance,
                                       double max_distance) {
  // Given this information, create 6 bounding planes, assuming the camera is
  // pointing with in the positive X direction.
  corners_C_.clear();
  corners_C_.reserve(8);

  double horizontal_fov = horizontal_fov_deg * M_PI / 180.0;
  double vertical_fov = vertical_fov_deg * M_PI / 180.0;

  double tan_half_hfov = std::tan(horizontal_fov / 2.0);
  double tan_half_vfov = std::tan(vertical_fov / 2.0);

  VLOG(5) << " Tan half FOV: Horizontal: " << tan_half_hfov
          << " Vertical: " << tan_half_vfov;

  // Near Plane corners
  corners_C_.emplace_back(Point(min_distance, min_distance * tan_half_hfov,
                                min_distance * tan_half_vfov));
  VLOG(5) << "Near plane: Corner 0: " << corners_C_[0].transpose();
  corners_C_.emplace_back(Point(min_distance, min_distance * tan_half_hfov,
                                -min_distance * tan_half_vfov));
  VLOG(5) << "Near plane: Corner 1: " << corners_C_[1].transpose();
  corners_C_.emplace_back(Point(min_distance, -min_distance * tan_half_hfov,
                                -min_distance * tan_half_vfov));
  VLOG(5) << "Near plane: Corner 2: " << corners_C_[2].transpose();
  corners_C_.emplace_back(Point(min_distance, -min_distance * tan_half_hfov,
                                min_distance * tan_half_vfov));
  VLOG(5) << "Near plane: Corner 3: " << corners_C_[3].transpose();

  // Far Plane corners
  corners_C_.emplace_back(Point(max_distance, max_distance * tan_half_hfov,
                                max_distance * tan_half_vfov));
  corners_C_.emplace_back(Point(max_distance, max_distance * tan_half_hfov,
                                -max_distance * tan_half_vfov));
  corners_C_.emplace_back(Point(max_distance, -max_distance * tan_half_hfov,
                                -max_distance * tan_half_vfov));
  corners_C_.emplace_back(Point(max_distance, -max_distance * tan_half_hfov,
                                max_distance * tan_half_vfov));

  initialized_ = true;
}

Transformation CameraModel::getCameraPose() const { return T_G_C_; }

void CameraModel::setCameraPose(const Transformation &cam_pose) {
  T_G_C_ = cam_pose;
  calculateBoundingPlanes();
}

void CameraModel::setBodyPose(const Transformation &body_pose) {
  setCameraPose(body_pose * T_C_B_.inverse());
}

void CameraModel::setExtrinsics(const Transformation &T_C_B) { T_C_B_ = T_C_B; }

void CameraModel::calculateBoundingPlanes() {
  if (!initialized_) {
    return;
  }

  CHECK_EQ(corners_C_.size(), 8u);
  if (bounding_planes_.empty()) {
    bounding_planes_.resize(6);
  }

  AlignedVector<Point> corners_G;
  corners_G.resize(corners_C_.size());

  // Transform all the points.
  for (size_t i = 0; i < corners_C_.size(); ++i) {
    corners_G[i] = T_G_C_ * corners_C_[i];
  }

  // Near plane.
  bounding_planes_[0].setFromPoints(corners_G[0], corners_G[2], corners_G[1]);
  VLOG(5) << "Near plane: Normal: " << bounding_planes_[0].normal().transpose()
          << " distance: " << bounding_planes_[0].distance();
  // Far plane.
  bounding_planes_[1].setFromPoints(corners_G[4], corners_G[5], corners_G[6]);
  VLOG(5) << "Far plane: Normal: " << bounding_planes_[1].normal().transpose()
          << " distance: " << bounding_planes_[1].distance();

  // Left.
  bounding_planes_[2].setFromPoints(corners_G[3], corners_G[6], corners_G[2]);
  VLOG(5) << "Left plane: Normal: " << bounding_planes_[2].normal().transpose()
          << " distance: " << bounding_planes_[2].distance();

  // Right.
  bounding_planes_[3].setFromPoints(corners_G[0], corners_G[5], corners_G[4]);
  VLOG(5) << "Right plane: Normal: " << bounding_planes_[3].normal().transpose()
          << " distance: " << bounding_planes_[3].distance();

  // Top.
  bounding_planes_[4].setFromPoints(corners_G[3], corners_G[4], corners_G[7]);
  VLOG(5) << "Top plane: Normal: " << bounding_planes_[4].normal().transpose()
          << " distance: " << bounding_planes_[4].distance();

  // Bottom.
  bounding_planes_[5].setFromPoints(corners_G[2], corners_G[6], corners_G[5]);
  VLOG(5) << "Bottom plane: Normal: "
          << bounding_planes_[5].normal().transpose()
          << " distance: " << bounding_planes_[5].distance();

  // Calculate AABB.
  aabb_min_.setConstant(std::numeric_limits<double>::max());
  aabb_max_.setConstant(std::numeric_limits<double>::lowest());

  for (int i = 0; i < 3; i++) {
    for (auto &j : corners_G) {
      aabb_min_(i) = std::min(aabb_min_(i), j(i));
      aabb_max_(i) = std::max(aabb_max_(i), j(i));
    }
  }

  VLOG(5) << "AABB min:\n"
          << aabb_min_.transpose() << "\nAABB max:\n"
          << aabb_max_.transpose();
}

void CameraModel::getAabb(Point *aabb_min, Point *aabb_max) const {
  *aabb_min = aabb_min_;
  *aabb_max = aabb_max_;
}

bool CameraModel::isPointInView(const Point &point) const {
  for (const auto &bounding_plane : bounding_planes_) {
    if (!bounding_plane.isPointCorrectSide(point)) {
      return false;
    }
  }
  return true;
}

void CameraModel::getBoundingLines(AlignedVector<Point> *lines) const {
  CHECK_NOTNULL(lines);
  lines->clear();
  lines->reserve(26);

  // Transform the points again... This is just for visualization so we can
  // waste a bit more time here.
  AlignedVector<Point> corners_G;
  // Untransformed corners are in the camera coordinate frame (corners_C_).
  corners_G.resize(corners_C_.size());

  // Transform all the points.
  for (size_t i = 0; i < corners_C_.size(); ++i) {
    corners_G[i] = T_G_C_ * corners_C_[i];
  }

  // All pairs of lines.
  lines->push_back(corners_G[0]);
  lines->push_back(corners_G[1]);

  lines->push_back(corners_G[1]);
  lines->push_back(corners_G[2]);

  lines->push_back(corners_G[2]);
  lines->push_back(corners_G[3]);

  lines->push_back(corners_G[3]);
  lines->push_back(corners_G[0]);

  lines->push_back(corners_G[4]);
  lines->push_back(corners_G[5]);

  lines->push_back(corners_G[5]);
  lines->push_back(corners_G[6]);

  lines->push_back(corners_G[6]);
  lines->push_back(corners_G[7]);

  lines->push_back(corners_G[7]);
  lines->push_back(corners_G[4]);

  lines->push_back(corners_G[0]);
  lines->push_back(corners_G[4]);

  lines->push_back(corners_G[1]);
  lines->push_back(corners_G[5]);

  lines->push_back(corners_G[3]);
  lines->push_back(corners_G[7]);

  lines->push_back(corners_G[2]);
  lines->push_back(corners_G[6]);

  lines->push_back(corners_G[0]);
  lines->push_back(bounding_planes_[0].normal());

  VLOG(5) << "Is point in correct side: "
          << bounding_planes_[0].isPointCorrectSide(Point(-1, 0, 0));
}

void CameraModel::getFarPlanePoints(AlignedVector<Point> *points) const {
  CHECK_NOTNULL(points);
  points->clear();
  points->reserve(3);

  points->emplace_back(T_G_C_ * corners_C_[4]);
  points->emplace_back(T_G_C_ * corners_C_[5]);
  points->emplace_back(T_G_C_ * corners_C_[6]);
}