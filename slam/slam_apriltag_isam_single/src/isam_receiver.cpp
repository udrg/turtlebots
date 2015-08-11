
#include "slam_apriltag_isam/isam_receiver.h"

int main(int argc, char **argv)
{
  ros::init(argc, argv, "isam_receiver");
  ros::NodeHandle nh;
  iSAMprocessor processor(nh);
  while(ros::ok()){
      ros::spinOnce(); 
  }
  ROS_INFO_STREAM("good ");
processor.Finish("1.txt");
}


iSAMprocessor::iSAMprocessor(ros::NodeHandle& nh){

  //initialize Publisher and subscriber
  april_sub = nh.subscribe<slam_apriltag_isam_single::PoseStampedArray>("/tag_detections_pose", 1,
                                                     &iSAMprocessor::AprilCallback,this);
  marker_pub = nh.advertise<visualization_msgs::Marker>("path_tag", 1);
  copyEnable=true;
  InitMarker();
  InitSlam();

}
// shutdown the rosnode
iSAMprocessor::~iSAMprocessor(){
  ros::shutdown();
}
//save the PoseArray and odom Information
void iSAMprocessor::AprilCallback(const slam_apriltag_isam_single::PoseStampedArrayConstPtr& PoseArray){

  new_posearray.header=PoseArray->header;

  new_posearray.poses=PoseArray->poses;

  new_orientation=tf::Quaternion(PoseArray->odom.pose.pose.orientation.x, PoseArray->odom.pose.pose.orientation.y,
                                 PoseArray->odom.pose.pose.orientation.z, PoseArray->odom.pose.pose.orientation.w);;
  new_position=tf::Vector3(PoseArray->odom.pose.pose.position.x,PoseArray->odom.pose.pose.position.y,PoseArray->odom.pose.pose.position.z);
  if(copyEnable){
    old_position = new_position;
    old_orientation = new_orientation;
    copyEnable=false;
  }
  Processing();

}


//init the slam and set the parameters create first node.
void iSAMprocessor::InitSlam() {
  //set noise
  noise3 = Information(100. * eye(3));
  noise2 = Information(100. * eye(2));

  // create a first pose (a node)
  Pose2d_Node* new_pose_node = new Pose2d_Node();
  // add it to the graph
  slam.add_node(new_pose_node);
  // create a prior measurement (a factor)
  pose_nodes.push_back(new_pose_node);
  Pose2d origin(0., 0., 0.);
  Pose2d_Factor* prior = new Pose2d_Factor(pose_nodes[0], origin, noise3);
  // add it to the graph
  slam.add_factor(prior);
  }
void iSAMprocessor::InitMarker(){
  // set the frame_id
  tags.header.frame_id = line_strip.header.frame_id  = "/my_frame";
  //set time stamp
  tags.header.stamp = line_strip.header.stamp  = ros::Time::now();
  //set namespace
  tags.ns = line_strip.ns = "tags_and_lines";
  //set actions
  tags.action = line_strip.action = visualization_msgs::Marker::ADD;
  tags.pose.orientation.w = line_strip.pose.orientation.w = 1.0;
  tags.id=0;
  line_strip.id=1;
  //set type
  tags.type=visualization_msgs::Marker::CUBE_LIST;
  line_strip.type = visualization_msgs::Marker::LINE_STRIP;
  //set cube size
  tags.scale.x=0.1;
  tags.scale.y=0.1;
  tags.scale.z=0.1;
  //set line width
  line_strip.scale.x = 0.01;
  //set color
  tags.color.g=1.0;
  tags.color.a=1.0;
  line_strip.color.r=1.0;
  line_strip.color.a=1.0;
  // publish the path after isam process
  //isam_path_pub = nh.advertise<nav_msgs::Path>("isam_path", 1);
}
//add new node to slam, distance: distance between last and secondlast nodes,
//angle: angle difference between last and secondlast nodes
void iSAMprocessor::AddNode(double distance,double angle){

  // create new node and put into pose_nodes
  Pose2d_Node* new_pose_node = new Pose2d_Node();
  slam.add_node(new_pose_node);
  pose_nodes.push_back(new_pose_node);

  //relation between this and previous nodes
  Pose2d odometry(distance, 0., angle);
  //create constraint
  Pose2d_Pose2d_Factor* constraint =
  new Pose2d_Pose2d_Factor(pose_nodes[pose_nodes.size()-2], pose_nodes[pose_nodes.size()-1], odometry, noise3);
  //add constraint to slam
  slam.add_factor(constraint);
}
void iSAMprocessor::AddPoint(){
  for(int i = 0; i < new_posearray.poses.size(); i++){
    int id = FindID(new_posearray.poses[i].header.frame_id);
    // if did not find id in list than add new point
    if(id == point_nodes.size()){
      Point2d_Node* new_point_node = new Point2d_Node();
      slam.add_node(new_point_node);
      point_nodes.push_back(new_point_node);
    }
    //create new measure

    Point2d measure((new_posearray.poses[i].pose.position.z-0.1),new_posearray.poses[i].pose.position.x * (-1.));
    //create measurement
    Pose2d_Point2d_Factor* measurement =
      new Pose2d_Point2d_Factor(pose_nodes[pose_nodes.size() - 1],
                                point_nodes[id], measure, noise2);
    //add measurement to slam
    slam.add_factor(measurement);
  }
}
void iSAMprocessor::PublishMarker(){
  //clear the line_strip
  line_strip.points.clear();
  //set the current time
  tags.header.stamp = line_strip.header.stamp  = ros::Time::now();
  //add each node to line_strip after slam update
  for(int i=1; i < pose_nodes.size(); i++){
    geometry_msgs::Point p;
    p.x = pose_nodes[i]->value().x();
    p.y = pose_nodes[i]->value().y();
    p.z = 0;
    line_strip.points.push_back(p);
  }
  //clear tags
  tags.points.clear();
  //add each tag to tags after slam update
  for(int i = 0; i < point_nodes.size(); i++){
    geometry_msgs::Point p;
    p.x = point_nodes[i]->value().x();
    p.y = point_nodes[i]->value().y();
    p.z = 0;
    tags.points.push_back(p);
  }
  //publish the markers
  marker_pub.publish(tags);
  marker_pub.publish(line_strip);

  //broadcast the tf for rviz
  static tf::TransformBroadcaster br;
  tf::Transform transform;
  transform.setOrigin( tf::Vector3(0.0, 0.0, 0.0) );
  tf::Quaternion q;
  q.setRPY(0, 0, 0);
  transform.setRotation(q);

  br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "path_tag", "/my_frame"));
}
void iSAMprocessor::Processing(){


  //angle between two pose, (angle * axis at z) is the actual angle of turtlebot
  double angle = new_orientation.getAngle() * new_orientation.getAxis().getZ() -
  old_orientation.getAngle() * new_orientation.getAxis().getZ();
  //distance between two pose
  double distance = new_position.distance(old_position);
  //if the changes meets the restrain, moving distance or angle larger than setted value
  if(distance > 0.05 || abs(angle) > 0.01){
    AddNode(distance,angle);

    AddPoint();

    // save the new to the old
    old_position = new_position;
    old_orientation = new_orientation;
    // update the map

    slam.update();

    PublishMarker();
  }
}

// find the landmark index number base on time.
int iSAMprocessor::FindID(string ID){
//get the landmark number
    for(int i=0;i<ID_array.size();i++){
      if(ID==ID_array[i]){
        return i;
      }
    }

    ID_array.push_back(ID);

    return ID_array.size()-1;
}
//save the processed mapping information to the file example: run1.txt
void iSAMprocessor::Finish(string name){
  slam.batch_optimization();
  slam.print_graph();
  slam.save(name);
}
