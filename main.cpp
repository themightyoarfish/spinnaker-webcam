#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>

#include "SpinGenApi/SpinnakerGenApi.h"
#include "Spinnaker.h"

using std::string;
using namespace Spinnaker::GenApi;

static Spinnaker::SystemPtr spinnaker_system = nullptr;
static Spinnaker::CameraPtr camera = nullptr;
static Spinnaker::CameraList camList;
static Spinnaker::ImagePtr currentFrame = nullptr;

static bool stop = false;

void shutdown_camera(int signal) {
  stop = true;
  std::cout << "Shitting down cameras." << std::endl;

  if (currentFrame) {
    try {
      std::cout << "Release current frame" << std::endl;
      currentFrame->Release();
    } catch (Spinnaker::Exception &e) {
      std::cout << "Caught error " << e.what() << std::endl;
    }
    currentFrame = nullptr;
  }
  std::cout << "End acquisition" << std::endl;
  camera->EndAcquisition();
  std::cout << "Deinit camera" << std::endl;
  camera->DeInit();
  // when spinnaker_system  is released in same scope, camera ptr must be
  // cleaned up before
  camera = nullptr;
  std::cout << "Clear camera list" << std::endl;
  camList.Clear();
  std::cout << "Release system" << std::endl;
  camList.Clear();
  spinnaker_system->ReleaseInstance();
  spinnaker_system = nullptr;
}

int setCameraSetting(const string &node, const string &value) {
  INodeMap &nodeMap = camera->GetNodeMap();

  // Retrieve enumeration node from nodemap
  CEnumerationPtr ptr = nodeMap.GetNode(node.c_str());
  if (!IsAvailable(ptr)) {
    return -1;
  }
  if (!IsWritable(ptr)) {
    return -1;
  }
  // Retrieve entry node from enumeration node
  CEnumEntryPtr ptrValue = ptr->GetEntryByName(value.c_str());
  if (!IsAvailable(ptrValue)) {
    return -1;
  }
  if (!IsReadable(ptrValue)) {
    return -1;
  }
  // retrieve value from entry node
  const int64_t valueToSet = ptrValue->GetValue();

  // Set value from entry node as new value of enumeration node
  ptr->SetIntValue(valueToSet);

  return 0;
}

int setCameraSetting(const string &node, int val) {
  INodeMap &nodeMap = camera->GetNodeMap();

  CIntegerPtr ptr = nodeMap.GetNode(node.c_str());
  if (!IsAvailable(ptr)) {
    return -1;
  }
  if (!IsWritable(ptr)) {
    return -1;
  }
  ptr->SetValue(val);
  return 0;
}
int setCameraSetting(const string &node, float val) {
  INodeMap &nodeMap = camera->GetNodeMap();
  CFloatPtr ptr = nodeMap.GetNode(node.c_str());
  if (!IsAvailable(ptr)) {
    return -1;
  }
  if (!IsWritable(ptr)) {
    return -1;
  }
  ptr->SetValue(val);

  return 0;
}
int setCameraSetting(const string &node, bool val) {
  INodeMap &nodeMap = camera->GetNodeMap();

  CBooleanPtr ptr = nodeMap.GetNode(node.c_str());
  if (!IsAvailable(ptr)) {
    return -1;
  }
  if (!IsWritable(ptr)) {
    return -1;
  }
  ptr->SetValue(val);

  return 0;
}

int setPixFmt() {
  return setCameraSetting("PixelFormat", string("RGB8Packed"));
}

void resetUserSet() {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  camera->UserSetSelector.SetValue(
      Spinnaker::UserSetSelectorEnums::UserSetSelector_Default);

  camera->UserSetLoad.Execute();
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

int setROI(int offsetX, int offsetY, int imwidth, int imheight) {
  // remove offsets, to allow large(r) image areas
  if (setCameraSetting("OffsetX", 0) == -1) {
    return -1;
  }
  if (setCameraSetting("OffsetY", 0) == -1) {
    return -1;
  }
  // now set specified image areas
  if (setCameraSetting("Height", imheight) == -1) {
    return -1;
  }
  if (setCameraSetting("Width", imwidth) == -1) {
    return -1;
  }
  // now set offsets, may fail, if too large for above image area
  if (setCameraSetting("OffsetX", offsetX) == -1) {
    return -1;
  }
  if (setCameraSetting("OffsetY", offsetY) == -1) {
    return -1;
  }

  return 0;
}
int setExposureTime(float exposure_time) {
  return setCameraSetting("ExposureTime", exposure_time);
}

int setExposureAuto(const string &mode) {
  return setCameraSetting("ExposureAuto", mode);
}

int setGainAutoDisable() { return setCameraSetting("GainAuto", string("Off")); }

int setSharpeningDisable() {
  return setCameraSetting("SharpeningEnable", false);
}

int setWhiteBalanceAuto() {
  return setCameraSetting("BalanceWhiteAuto", string("Continuous"));
}

static int init_webcam(const std::string &device_name, int width, int height,
                       int rowsize) {
  /************************************************************************************
   *                               Init webcam devices *
   ************************************************************************************/
  struct v4l2_format v4l2_fmt;
  struct v4l2_capability capability;
  int device_fd = open(device_name.c_str(), O_RDWR);

  if (device_fd == -1) {
    throw std::runtime_error("Could not open " + device_name);
  }
  if (-1 == ioctl(device_fd, VIDIOC_QUERYCAP, &capability)) {
    throw std::runtime_error("Could not query device cap.");
  }
  v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  if (-1 == ioctl(device_fd, VIDIOC_G_FMT, &v4l2_fmt)) {
    throw std::runtime_error("Could not query video format.");
  }
  v4l2_fmt.fmt.pix.width = width;
  v4l2_fmt.fmt.pix.height = height;
  v4l2_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
  v4l2_fmt.fmt.pix.bytesperline = width * 3;
  v4l2_fmt.fmt.pix.sizeimage = width * height * 3;
  v4l2_fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (-1 == ioctl(device_fd, VIDIOC_S_FMT, &v4l2_fmt)) {
    throw std::runtime_error("Could not set video format.");
  }

  return device_fd;
}

int main(int argc, char *argv[]) {
  std::signal(SIGINT, shutdown_camera);

  constexpr int width = 1280;
  constexpr int height = 1024;
  constexpr int offsetX = 0;
  constexpr int offsetY = 0;
  std::string serial = "19450079";

  std::string device_name = "/dev/video0";

  if (argc > 1) {
    serial = argv[1];
  }

  if (argc > 2) {
    device_name = argv[2];
  }

  spinnaker_system = Spinnaker::System::GetInstance();

  // Retrieve list of cameras from the spinnaker_system
  camList = spinnaker_system->GetCameras();

  camera = camList.GetBySerial(serial);

  camList.Clear();

  // Initialize camera
  camera->Init();

  resetUserSet();

  // Set acquisition mode to continuous
  if (setCameraSetting("AcquisitionMode", string("Continuous")) == -1) {
    throw std::runtime_error("Could not set AcquisitionMode");
  }

  setCameraSetting("AcquisitionFrameRateEnabled", true);
  setCameraSetting("AcquisitionFrameRateEnable", true);
  setCameraSetting("AcquisitionFrameRateAuto", std::string("On"));

  // Important, otherwise we don't get frames at all
  if (setPixFmt() == -1) {
    throw std::runtime_error("Could not set pixel format");
  }

  // Disable auto exposure to be safe. otherwise we cannot manually set
  // exposure time.
  setCameraSetting("ExposureAuto", string("On"));

  setROI(offsetX, offsetY, width, height);

  camera->BeginAcquisition();

  int device_fd = -1;

  // wait a bit for camera to start streaming
  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cout << "Beginning capture." << std::endl;
  // try 100 times to get a frame with some retry logic. we wait at the end to
  // retrieve only every 30ms
  while (!stop) {
    try {
      currentFrame = camera->GetNextImage(10);
      if (currentFrame->IsIncomplete()) {
        currentFrame->Release();
        currentFrame = nullptr;
      } else if (currentFrame->GetImageStatus() != Spinnaker::IMAGE_NO_ERROR) {
        currentFrame->Release();
        currentFrame = nullptr;
      }
    } catch (const Spinnaker::Exception &e) {
    }

    if (currentFrame) {
      if (device_fd == -1) {
        device_fd =
            init_webcam(device_name, currentFrame->GetWidth(),
                        currentFrame->GetHeight(), currentFrame->GetStride());
      }
      int bytes_written = write(device_fd, currentFrame->GetData(),
                                currentFrame->GetBufferSize());
      currentFrame->Release();
      currentFrame = nullptr;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return 0;
}
