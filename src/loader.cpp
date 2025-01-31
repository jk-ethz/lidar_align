#include <geometry_msgs/TransformStamped.h>
#define LZ4_stream_t LZ4_stream_t_deprecated
#define LZ4_resetStream LZ4_resetStream_deprecated
#define LZ4_createStream LZ4_createStream_deprecated
#define LZ4_freeStream LZ4_freeStream_deprecated
#define LZ4_loadDict LZ4_loadDict_deprecated
#define LZ4_compress_fast_continue LZ4_compress_fast_continue_deprecated
#define LZ4_saveDict LZ4_saveDict_deprecated
#define LZ4_streamDecode_t LZ4_streamDecode_t_deprecated
#define LZ4_compress_continue LZ4_compress_continue_deprecated
#define LZ4_compress_limitedOutput_continue LZ4_compress_limitedOutput_continue_deprecated
#define LZ4_createStreamDecode LZ4_createStreamDecode_deprecated
#define LZ4_freeStreamDecode LZ4_freeStreamDecode_deprecated
#define LZ4_setStreamDecode LZ4_setStreamDecode_deprecated
#define LZ4_decompress_safe_continue LZ4_decompress_safe_continue_deprecated
#define LZ4_decompress_fast_continue LZ4_decompress_fast_continue_deprecated
#include <rosbag/bag.h>
#undef LZ4_stream_t
#undef LZ4_resetStream
#undef LZ4_createStream
#undef LZ4_freeStream
#undef LZ4_loadDict
#undef LZ4_compress_fast_continue
#undef LZ4_saveDict
#undef LZ4_streamDecode_t
#undef LZ4_compress_continue
#undef LZ4_compress_limitedOutput_continue
#undef LZ4_createStreamDecode
#undef LZ4_freeStreamDecode
#undef LZ4_setStreamDecode
#undef LZ4_decompress_safe_continue
#undef LZ4_decompress_fast_continue
#include <rosbag/view.h>
#include <pcl/filters/voxel_grid.h>

#include "lidar_align/loader.h"
#include "lidar_align/transform.h"

namespace lidar_align {

Loader::Loader(const Config& config) : config_(config) {}

Loader::Config Loader::getConfig(ros::NodeHandle* nh) {
  Loader::Config config;
  nh->param("use_n_scans", config.use_n_scans, config.use_n_scans);
  return config;
}

void Loader::parsePointcloudMsg(const sensor_msgs::PointCloud2 msg,
                                LoaderPointcloud* pointcloud) {
  bool has_timing = false;
  bool has_intensity = false;
  for (const sensor_msgs::PointField& field : msg.fields) {
    if (field.name == "time_offset_us") {
      has_timing = true;
    } else if (field.name == "intensity") {
      has_intensity = true;
    }
  }

  if (has_timing) {
    pcl::fromROSMsg(msg, *pointcloud);
    return;
  } else if (has_intensity) {
    Pointcloud raw_pointcloud;
    pcl::fromROSMsg(msg, raw_pointcloud);

    for (const Point& raw_point : raw_pointcloud) {
      PointAllFields point;
      point.x = raw_point.x;
      point.y = raw_point.y;
      point.z = raw_point.z;
      point.intensity = raw_point.intensity;

      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          !std::isfinite(point.z) || !std::isfinite(point.intensity)) {
        continue;
      }

      pointcloud->push_back(point);
    }
    pointcloud->header = raw_pointcloud.header;
  } else {
    pcl::PointCloud<pcl::PointXYZ>::Ptr raw_pointcloud_ptr(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>& raw_pointcloud = *raw_pointcloud_ptr;
    pcl::fromROSMsg(msg, raw_pointcloud);
    
    const size_t size_before_filter = raw_pointcloud.size();
    pcl::VoxelGrid<pcl::PointXYZ> vg;
    vg.setLeafSize(0.01, 0.01, 0.01);
    vg.setInputCloud(raw_pointcloud_ptr);
    vg.filter(raw_pointcloud);
    std::cout << "Size before filter: " << size_before_filter << ", size after: " << raw_pointcloud.size() << std::endl;

    for (const pcl::PointXYZ& raw_point : raw_pointcloud) {
      PointAllFields point;
      point.x = raw_point.x;
      point.y = raw_point.y;
      point.z = raw_point.z;

      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          !std::isfinite(point.z)) {
        continue;
      }

      pointcloud->push_back(point);
    }
    pointcloud->header = raw_pointcloud.header;
  }
}

bool Loader::loadPointcloudFromROSBag(const std::string& bag_path,
                                      const Scan::Config& scan_config,
                                      Lidar* lidar) {
  rosbag::Bag bag;
  try {
    bag.open(bag_path, rosbag::bagmode::Read);
  } catch (rosbag::BagException e) {
    ROS_ERROR_STREAM("LOADING BAG FAILED: " << e.what());
    return false;
  }

  std::vector<std::string> types;
  types.push_back(std::string("sensor_msgs/PointCloud2"));
  rosbag::View view(bag, rosbag::TypeQuery(types));

  size_t scan_num = 0;
  for (const rosbag::MessageInstance& m : view) {
    std::cout << " Loading scan: \e[1m" << scan_num++ << "\e[0m from ros bag"
              << '\r' << std::flush;

    LoaderPointcloud pointcloud;
    parsePointcloudMsg(*(m.instantiate<sensor_msgs::PointCloud2>()),
                       &pointcloud);

    lidar->addPointcloud(pointcloud, scan_config);

    if (lidar->getNumberOfScans() >= config_.use_n_scans) {
      break;
    }
  }
  if (lidar->getTotalPoints() == 0) {
    ROS_ERROR_STREAM(
        "No points were loaded, verify that the bag contains populated "
        "messages of type sensor_msgs/PointCloud2");
    return false;
  }

  return true;
}

bool Loader::loadTformFromROSBag(const std::string& bag_path, Odom* odom) {
  rosbag::Bag bag;
  try {
    bag.open(bag_path, rosbag::bagmode::Read);
  } catch (rosbag::BagException e) {
    ROS_ERROR_STREAM("LOADING BAG FAILED: " << e.what());
    return false;
  }

  std::vector<std::string> types;
  types.push_back(std::string("geometry_msgs/TransformStamped"));
  rosbag::View view(bag, rosbag::TypeQuery(types));

  size_t tform_num = 0;
  for (const rosbag::MessageInstance& m : view) {
    std::cout << " Loading transform: \e[1m" << tform_num++
              << "\e[0m from ros bag" << '\r' << std::flush;

    geometry_msgs::TransformStamped transform_msg =
        *(m.instantiate<geometry_msgs::TransformStamped>());

    Timestamp stamp = transform_msg.header.stamp.sec * 1000000ll +
                      transform_msg.header.stamp.nsec / 1000ll;

    Transform T(Transform::Translation(transform_msg.transform.translation.x,
                                       transform_msg.transform.translation.y,
                                       transform_msg.transform.translation.z),
                Transform::Rotation(transform_msg.transform.rotation.w,
                                    transform_msg.transform.rotation.x,
                                    transform_msg.transform.rotation.y,
                                    transform_msg.transform.rotation.z));
    odom->addTransformData(stamp, T);
  }

  if (odom->empty()) {
    ROS_ERROR_STREAM("No odom messages found!");
    return false;
  }

  return true;
}

bool Loader::loadTformFromMaplabCSV(const std::string& csv_path, Odom* odom) {
  std::ifstream file(csv_path, std::ifstream::in);

  size_t tform_num = 0;
  while (file.peek() != EOF) {
    std::cout << " Loading transform: \e[1m" << tform_num++
              << "\e[0m from csv file" << '\r' << std::flush;

    Timestamp stamp;
    Transform T;

    if (getNextCSVTransform(file, &stamp, &T)) {
      odom->addTransformData(stamp, T);
    }
  }

  return true;
}

// lots of potential failure cases not checked
bool Loader::getNextCSVTransform(std::istream& str, Timestamp* stamp,
                                 Transform* T) {
  std::string line;
  std::getline(str, line);

  // ignore comment lines
  if (line[0] == '#') {
    return false;
  }

  std::stringstream line_stream(line);
  std::string cell;

  std::vector<std::string> data;
  while (std::getline(line_stream, cell, ',')) {
    data.push_back(cell);
  }

  if (data.size() < 9) {
    return false;
  }

  constexpr size_t TIME = 0;
  constexpr size_t X = 2;
  constexpr size_t Y = 3;
  constexpr size_t Z = 4;
  constexpr size_t RW = 5;
  constexpr size_t RX = 6;
  constexpr size_t RY = 7;
  constexpr size_t RZ = 8;

  *stamp = std::stoll(data[TIME]) / 1000ll;
  *T = Transform(Transform::Translation(std::stod(data[X]), std::stod(data[Y]),
                                        std::stod(data[Z])),
                 Transform::Rotation(std::stod(data[RW]), std::stod(data[RX]),
                                     std::stod(data[RY]), std::stod(data[RZ])));

  return true;
}

}  // namespace lidar_align
