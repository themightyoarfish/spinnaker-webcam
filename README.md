# spinnaker-webcam

## How to (Ubuntu 16.04)

0. Install the spinnaker SDK
1. Install [v4l2-loopback](https://github.com/umlaeute/v4l2loopback/) via `sudo apt -y install 4l2loopback-dkms`
2. Insert the v4l2 kernel module via `modprobe v4l2loopback exclusive_caps=1 card_label="VirtualCam"`. This will create a virtual video device `/dev/videoX` where X is a number. Check the v4l2-loopback documentation for configuration options
3. `mkdir build && cd build && cmake .. && make` to build the binary.
4. Run `./build/spinnaker-webcam [serial] [device]`, where device defaults to
   `/dev/video0`

## Limitations

0. Camera cannot  be  shutdown correctly because the functions don't return. Probably
   just the usual Spinnaker bugs wich won't reproduce anywhere else
1. Only works in Chrome. Firefox fails with some error about starting video. This may be
   because it doesn't support 24 bit packed rgb image format, who knows.
