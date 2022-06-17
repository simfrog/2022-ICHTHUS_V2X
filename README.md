# ichthus_v2x

V2X ROS2 node of Soongsil University, Ichthus


## Process
 1. Receive J2735(SPaT) Messages  
 Recevie SPaT(Traffic light Information) Messages through WAVE socket communication and publish "/v2x_info" ros2 topic  
 
 2. Send J2735(PVD) Messages  
 Subscribe other ros node topic, fill the correct PVD message and send PVD message through WAVE socket communication  
 
## ROS API
#### Subs
* > /fix
