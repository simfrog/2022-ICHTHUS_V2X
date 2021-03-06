#include "ichthus_v2x/ichthus_v2x.hpp"


namespace ichthus_v2x
{

rclcpp::Time prev_time;
bool done = false;
bool is_first_topic = true;

IchthusV2X::IchthusV2X() : Node("ichthus_v2x") 
{
  install_signal_handler();
  auto qos_profile = rclcpp::QoS(rclcpp::KeepLast(10));
  v2x_pub_ = this->create_publisher<kiapi_msgs::msg::V2xinfo>("v2x_info", qos_profile);
  loc_sub_ = this->create_subscription<sensor_msgs::msg::NavSatFix>("/fix", 
              rclcpp::SensorDataQoS().keep_last(64),
              std::bind(&IchthusV2X::GnssCallback, this, std::placeholders::_1));
  // gnss_pose_sub_ = this->create_subscription<geometry_msgs::msg::PoseStamped>("/gnss_pose", 
  //             rclcpp::SensorDataQoS().keep_last(64),
  //             std::bind(&IchthusV2X::GnssPoseCallback, this, std::placeholders::_1));
  vel_sub_ = this->create_subscription<std_msgs::msg::Float64MultiArray>("/v2x", qos_profile,
              std::bind(&IchthusV2X::VelocityCallback, this, std::placeholders::_1));
  // pvd_pub_ = this->create_publisher<kiapi_msgs::msg::Pvdinfo>("pvd_info", qos_profile); /* DEBUG */
  // loc_sub_ = this->create_subscription<kiapi_msgs::msg::Mylocation>("gnss_info", qos_profile,
  //             std::bind(&IchthusV2X::LocationCallback, this, std::placeholders::_1));   /* DEBUG */
  ori_sub_ = this->create_subscription<sensor_msgs::msg::Imu>("/imu/data", qos_profile, /* DEBUG */
              std::bind(&IchthusV2X::ImuCallback, this, std::placeholders::_1));

  IP = this->declare_parameter("ip", "192.168.10.10");
  std::cout << "IP : " << IP << std::endl; /* DEBUG */
}
IchthusV2X::~IchthusV2X()
{
}

/*
void IchthusV2X::LocationCallback(const kiapi_msgs::msg::Mylocation::SharedPtr msg)
{
  std::cout << "callback" << std::endl;
  Vli_.latitude = msg->latitude;
  Vli_.longitude = msg->longitude;
  Vli_.elevation = msg->elevation;
  Vli_.heading = msg->heading;
  Vli_.speed = msg->speed;
  Vli_.transmisson = msg->transmisson;

  if(is_first_topic)
  {
    is_first_topic = false;
    connect_obu_uper_tcp();
  }
}
*/


void IchthusV2X::ImuCallback(const sensor_msgs::msg::Imu::SharedPtr msg)
{
  // std::cout << "callback" << std::endl;
  double siny_cosp = 2 * (msg->orientation.w * msg->orientation.z + msg->orientation.x * msg->orientation.y);
  double cosy_cosp = 1 - 2 * (msg->orientation.y * msg->orientation.y + msg->orientation.z * msg->orientation.z);
  Vli_.heading = std::atan2(siny_cosp, cosy_cosp);
}


void IchthusV2X::GnssCallback(const sensor_msgs::msg::NavSatFix::SharedPtr msg)
{
  // std::cout << "callback" << std::endl; /* DEBUG */
  Vli_.latitude = msg->latitude*10000000;
  Vli_.longitude = msg->longitude*10000000;
  Vli_.elevation = msg->altitude*10;

  if(is_first_topic)
  {
    is_first_topic = false;
    connect_obu_uper_tcp();
  }
}


// void IchthusV2X::GnssPoseCallback(const geometry_msgs::msg::PoseStamped::SharedPtr msg)
// {
//   // std::cout << "callback" << std::endl; /* DEBUG */
//   double siny_cosp = 2.0 * (msg->pose.orientation.w * msg->pose.orientation.z + msg->pose.orientation.x * msg->pose.orientation.y);
//   double cosy_cosp = 1.0 - 2.0 * (msg->pose.orientation.y * msg->pose.orientation.y + msg->pose.orientation.z * msg->pose.orientation.z);
//   Vli_.heading = static_cast<double>(static_cast<int>((-(std::atan2(siny_cosp, cosy_cosp) * (180.0 / M_PI)) + 450.0)) % 360)*80;
// }


void IchthusV2X::VelocityCallback(const std_msgs::msg::Float64MultiArray::SharedPtr msg)
{
  // std::cout << "velcallback1 : " << msg->data[0] << std::endl; /* DEBUG */
  // std::cout << "velcallback2 : " << msg->data[1] << std::endl; /* DEBUG */
  Vli_.speed = msg->data[0]*50;
  if (msg->data[1] == 0)
  {
    Vli_.transmisson = 1;
  }
  else if (msg->data[1] == 5)
  {
    Vli_.transmisson = 2;
  }
  else if (msg->data[1] == 6)
  {
    Vli_.transmisson = 0;
  }
  else if (msg->data[1] == 7)
  {
    Vli_.transmisson = 3;
  }
}

void IchthusV2X::connect_obu_uper_tcp()
{
  sockFd = -1;
  std::string obu_ip = IP;
  uint32_t obu_port = PORT;

  struct sockaddr_in obu_addr;
  memset(&obu_addr, 0, sizeof(obu_addr));
  obu_addr.sin_family = AF_INET;
  obu_addr.sin_port = htons(obu_port);

  /* Convert String IP Address */
  if (inet_pton(AF_INET, obu_ip.c_str(), &obu_addr.sin_addr) <= 0)
  {
    std::cout << "DEBUG : Error, inet_pton" << std::endl;
    exit(1);
  }

  /* Create Client Socket FD */
  if ((sockFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
  {
    std::cout << "DEBUG : connect failed, retry" << std::endl;
    exit(1);
  }

  /* Connect Client, Server TCP */
  int connected = -1;
  if ((connected = connect(sockFd, (const sockaddr *)&obu_addr, sizeof obu_addr))<0)
  {
    std::cout << "DEBUG : step sock connect error : " << connected << std::endl;
    exit(1);
  }

  txPvd = get_clock_time(); 

  std::cout << "DEBUG : OBU TCP [" << obu_ip << ":" << obu_port << "] Connected" << std::endl;

  /* Thread */
  th1 = receiveSpatThread();
  th2 = sendPvdThread();
}

unsigned long long IchthusV2X::get_clock_time()
{  
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);  
  uint64_t clock = ts.tv_sec * 1000 + (ts.tv_nsec / 1000000); 
  return clock;
}

thread IchthusV2X::receiveSpatThread()
{
  return thread([this]{receiveSpat();});
}
thread IchthusV2X::sendPvdThread()
{
  return thread([this]{sendPvd();});
}

void IchthusV2X::receiveSpat()
{
  int rxSize = -1;
  while ((rxSize = recv(sockFd, rxBuffer, 2048, MSG_NOSIGNAL)) > 0)
  {
    if(done) 
    {
      close(sockFd);
      exit(-1);
    }
    // std::cout << "Rx " << rxSize << " bytes" << std::endl; /* DEBUG */

    string msgs;
    msgs.append((char *)rxBuffer, sizeof(rxBuffer));

    if (msgs.size() < sizeof(struct CestObuUperPacketHeader))
    { 
      // fprintf(stdout, "require more bytes, current bytes = %d \n",msgs.size());
      std::cout <<  "require more bytes, current bytes = " << msgs.size() << std::endl;
      break;
    }
      
    std::string payload;
    struct CestObuUperPacketHeader *header = (struct CestObuUperPacketHeader *)&msgs[0]; 
    payload.append(msgs, sizeof(struct CestObuUperPacketHeader), header->payloadLen); 
    // std::cout << "size : " << header->payloadLen << endl; /* DEBUG */

    if(header->messageType == 0x3411)
    {
      struct TxWaveUperResultPayload *txpayload = (struct TxWaveUperResultPayload*)&payload[0];
      printf("RX - \"TX_WAVE_UPER_ACK\" [%d/%d/%d]\n", txpayload->txWaveUperSeq, txpayload->resultCode, txpayload->size);
      uperSize = 0;
    }
    else
    {
      printf("RX - \"RX_WAVE_UPER\" [%d]\n", header->payloadLen);
      // std::cout << "RX - \"RX_WAVE_UPER\" [" << header->payloadLen << "]" << std::endl;
    }

    MessageFrame_t* msgFrame = nullptr;
  
    auto res = uper_decode(0,&asn_DEF_MessageFrame,(void**)&msgFrame, payload.c_str(), header->payloadLen, 0,0);
    switch (res.code)
    {
      case asn_dec_rval_code_e::RC_OK:
          // fprintf(stderr,"\n[RC_OK]\n");
          // printf("\n[RC_OK]\n");
          std::cout << "\n[RC_OK]" << std::endl;
          switch (msgFrame->messageId)
          {
            case DSRC_ID_SPAT:
              std::cout << ">> Parse J2735 : SPAT <<" << endl;
              parse_spat((SPAT_t*)&msgFrame->value.choice.SPAT); 
              gen_SPaTMsg((SPAT_t*)&msgFrame->value.choice.SPAT);
              break;

            case DSRC_ID_MAP:
              std::cout << ">> Parse J2735 : MAP <<" << endl;

            default:
                break;
          }

          v2x_pub_->publish(v2x_msg);
          //asn_fprint(stderr, rxUperBuffer&asn_DEF_MessageFrame, msgFrame); /* DEBUG */

          break;
      case asn_dec_rval_code_e::RC_WMORE:
          // fprintf(stderr,"\n[RC_WMORE]\n");
          std::cout << "\n[RC_WMORE]" << std::endl;
          break;
      case asn_dec_rval_code_e::RC_FAIL:
          // fprintf(stderr,"\n[RC_FAIL]\n");
          std::cout << "\n[RC_FAIL]" << std::endl;
          break;
      default:
          break;
    }
      
    msgs.erase(0, sizeof(struct CestObuUperPacketHeader) + payload.size());
    // usleep(1000);
  }
  close(sockFd);
}

void IchthusV2X::gen_SPaTMsg(SPAT_t *spat)
{
  v2x_msg.msg_type = kiapi_msgs::msg::V2xinfo::SPAT_MSG_TYPE;

  v2x_msg.spat_id_region = 0;            // ????????? ?????? ?????? ID
  v2x_msg.spat_movement_cnt = 0;
  v2x_msg.spat_movement_name.resize(0);  // ????????? ??????
  v2x_msg.spat_signal_group.resize(0);   // ???????????? ???????????? ????????????
  v2x_msg.spat_eventstate.resize(0);     // ???,???,??? event???(????????????)
  v2x_msg.spat_minendtime.resize(0);     // ?????? minEndTime ??????????????? ??????.(?????? ?????? ?????? ??????)

  std::cout << "\ninit_gen_SPaT" << endl;
  for(int i = 0; i < spat->intersections.list.count; i++)
  {
    struct IntersectionState *inter_p = spat->intersections.list.array[i];
    v2x_msg.spat_id_region = inter_p->id.id;
    v2x_msg.spat_movement_cnt = inter_p->states.list.count;

    for (int j = 0 ; j < v2x_msg.spat_movement_cnt; ++j)
    {
      struct MovementState *move_p = inter_p->states.list.array[j];
      v2x_msg.spat_signal_group.push_back(move_p->signalGroup); 
      v2x_msg.spat_movement_name.push_back((char*)move_p->movementName->buf);
    
      for(int k = 0; k < move_p->state_time_speed.list.count; ++k)
      {
        struct MovementEvent *mvevent_p = move_p->state_time_speed.list.array[k];
        v2x_msg.spat_eventstate.push_back(mvevent_p->eventState);
          
        if (mvevent_p->timing)
        {
          v2x_msg.spat_minendtime.push_back(mvevent_p->timing->minEndTime);
        }
      }
    }
  }	
}

int IchthusV2X::parse_spat(SPAT_t *spat)
{ 
  for (int i = 0; i < spat->intersections.list.count; i++)
  {
    struct IntersectionState *ptr = spat->intersections.list.array[i];

    for (int i = 0; i < static_cast<int>(ptr->name->size); ++i)
    {
      printf("%c", ptr->name->buf[i]);
    }

    printf("\n");

    printf("id\n");
    printf("  region : %ld\n", *ptr->id.region);
    printf("  id     : %ld\n", ptr->id.id);
    printf("revision     : %ld\n", ptr->revision);

    printf("status       : 0x");
    for (int i = 0; i < static_cast<int>(ptr->status.size); ++i)
    {
      printf("%02X", ptr->status.buf[i]);
    }
    printf("\n");

    // printf("      revision     : %ld\n", ptr->revision);
    printf("moy          : %ld\n", *ptr->moy);
    printf("timeStamp    : %ld\n", *ptr->timeStamp);
      
    // if (ptr->enabledLanes) 
    // {
    //   printf("      enabledLanes : %ld\n", ptr->enabledLanes);
    // }
      
    if (ptr->states.list.count) 
    {
      printf("states       : %d items\n", ptr->states.list.count);
        
      for (int j = 0; j < ptr->states.list.count; ++j) 
      {
        printf("  states[%d]\n", j);
        printf("    movementName       : ");
          
        for (int l = 0; l < static_cast<int>(ptr->states.list.array[j]->movementName->size); ++l) 
        {
          printf("%c", ptr->states.list.array[j]->movementName->buf[l]);
        }
          
        printf("\n");
        printf("    signalGroup        : %ld\n", ptr->states.list.array[j]->signalGroup);
          
        if (ptr->states.list.array[j]->state_time_speed.list.count) 
        {
          printf("    state-time-speed   : %d items\n", ptr->states.list.array[j]->state_time_speed.list.count);
            
          for (int k = 0; k < ptr->states.list.array[j]->state_time_speed.list.count; ++k) 
          {
            struct MovementEvent *mvevent_p = ptr->states.list.array[j]->state_time_speed.list.array[k];
            
            printf("      Item %d\n", k);
            printf("        MovementEvent\n");
            printf("          eventState(%ld) : ", mvevent_p->eventState);
            switch (mvevent_p->eventState)
            {
							case e_MovementPhaseState::MovementPhaseState_unavailable:
                printf("unavailable\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_dark:
                printf("dark\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_stop_Then_Proceed:
                printf("stop_Then_Proceeddark\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_stop_And_Remain:
                printf("stop_And_Remain\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_pre_Movement:
                printf("pre_Movement\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_permissive_Movement_Allowed:
								printf("permissive_Movement_Allowed\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_protected_Movement_Allowed:
								printf("protected_Movement_Allowed\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_permissive_clearance:
								printf("permissive_clearance\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_protected_clearance:
								printf("protected_clearance\n");
                break;
							case e_MovementPhaseState::MovementPhaseState_caution_Conflicting_Traffic:
								printf("caution_Conflicting_Traffic\n");
                break;
							default:
								printf("Impossible situation happened. please check this\n");
                exit(-1);
						}

            printf("          timing\n");
            printf("            minEndTime : %ld\n", ptr->states.list.array[j]->state_time_speed.list.array[k]->timing->minEndTime);
          }
        }
        // fprintf(stdout, "          regional           : %ld\n", ptr->states.list.array[j]->regional);
      
        if (ptr->states.list.array[j]->maneuverAssistList->list.count)
        {
          printf("    maneuverAssistList : %d items\n", ptr->states.list.array[j]->maneuverAssistList->list.count);
        
          for (int j = 0; j < ptr->states.list.array[j]->maneuverAssistList->list.count; ++j) 
          {
            printf("      maneuverAssistList[%d]\n", j);
            printf("        connectionID     : %ld\n",  ptr->states.list.array[j]->maneuverAssistList->list.array[j]->connectionID);

            if (ptr->states.list.array[j]->maneuverAssistList->list.array[j]->pedBicycleDetect)
            {
              if (*ptr->states.list.array[j]->maneuverAssistList->list.array[j]->pedBicycleDetect == true)
              {
                printf("        pedBicycleDetect : True\n");
              }
              else
              {
                printf("        pedBicycleDetect : False\n");
              }
            }
          }
        }
      }
    }	
  }

  return 0;
}

/* TX_WAVE_UPER(RESULT) */
void IchthusV2X::sendPvd()
{
  while(1)
  {
    if(done) 
    {
      close(sockFd);
      exit(-1);
    }
    
    if(tx_v2i_pvd() < 0)
    {
      std::cout << "DEBUG : disconnect TCP, retry" << endl;
      close(sockFd);
      exit(1);
    }

    // usleep(1000);
  }
}

int IchthusV2X::tx_v2i_pvd()
{
  unsigned long long interval = get_clock_time() - txPvd;  // msec;

  if(interval < PVD_INTERVAL)
  {
    return 0;
  }

  txPvd += (interval - interval%PVD_INTERVAL); 

  MessageFrame_t msg;
  char uper[MAX_UPER_SIZE]; 

  fill_j2735_pvd(&msg);

  std::cout << ">> Parse J2735 : PVD <<" << endl;
  parse_pvd(&msg);
  gen_PvdMsg();

  // pvd_pub_->publish(pvd_msg); /* DEBUG */

  int encodedBits = encode_j2735_uper(uper, &msg);

  // printf("bits:%d\n",encodedBits); /*DEBUG */

  /* Encdoing Fail, unable to send */
  if(encodedBits < 0)
  {
    return 0;
  }
    
  int byteLen = encodedBits / 8 + ((encodedBits % 8)? 1:0);
    
  // print_hex(uper,byteLen); /* DEBUG */    
      
  return request_tx_wave_obu(uper,byteLen);  
}

void IchthusV2X::print_hex(char *data, int len)
{
  std::cout << "HEX[" << len << "] : ";
  for(int i = 0 ; i < len ; i++)
  {
    printf("%02X",(data[i] & 0xFF));
  }
  printf("\n");
}

int IchthusV2X::fill_j2735_pvd(MessageFrame_t *dst)
{
  /* current day, time */
  time_t timer;
  struct tm *t;
  timer = time(NULL);
  t = localtime(&timer);

  // ASN_STRUCT_RESET(asn_DEF_MessageFrame, dst); /* DEBUG */

  dst->messageId = 26; // Reference J2735.pdf - DE_DSRC_MessageID, probeVehicleData DSRCmsgID ::= 26 -- PVD 
  dst->value.present = MessageFrame__value_PR_ProbeVehicleData; // MessageFrame::value choice (asn1c)

  ProbeVehicleData_t *ptrPvd = &dst->value.choice.ProbeVehicleData;

  ptrPvd->timeStamp = NULL; // OPTIONAL, not to use
  ptrPvd->segNum = NULL;    // OPTIONAL, not to use
  ptrPvd->regional = NULL;  // OPTIONAL, not to use

  ptrPvd->probeID = new(struct VehicleIdent);
  ptrPvd->probeID->name = NULL;         // OPTIONAL, not to use
  ptrPvd->probeID->ownerCode = NULL;    // OPTIONAL, not to use
  ptrPvd->probeID->vehicleClass = NULL; // OPTIONAL, not to use
  ptrPvd->probeID->vin = NULL;          // OPTIONAL, not to use
  ptrPvd->probeID->vehicleType = NULL;  // OPTIONAL, not to use

  ptrPvd->probeID->id = new(struct VehicleID);
  ptrPvd->probeID->id->present = VehicleID_PR_entityID;   
  ptrPvd->probeID->id->present = VehicleID_PR_entityID;
  ptrPvd->probeID->id->choice.entityID.buf = (unsigned char *)malloc(4);
  ptrPvd->probeID->id->choice.entityID.size = 4; 
  ptrPvd->probeID->id->choice.entityID.buf[0] = 0xCE; // (INPUT) <---- ????????? ????????? ID ??????
  ptrPvd->probeID->id->choice.entityID.buf[1] = 0x24; // (INPUT) <---- ????????? ????????? ID ??????
  ptrPvd->probeID->id->choice.entityID.buf[2] = 0x67; // (INPUT) <---- ????????? ????????? ID ??????
  ptrPvd->probeID->id->choice.entityID.buf[3] = 0x04; // (INPUT) <---- ????????? ????????? ID ??????

  ptrPvd->startVector.utcTime = new(struct DDateTime);  
  ptrPvd->startVector.utcTime->year = new DYear_t;
  ptrPvd->startVector.utcTime->month = new DMonth_t; 
  ptrPvd->startVector.utcTime->day = new DDay_t; 
  ptrPvd->startVector.utcTime->hour = new DHour_t; 
  ptrPvd->startVector.utcTime->minute = new DMinute_t; 
  ptrPvd->startVector.utcTime->second = new DSecond_t;  
  ptrPvd->startVector.utcTime->offset = NULL; // OPTIONAL, not to use

  *ptrPvd->startVector.utcTime->year = t->tm_year+1900; // (INPUT) <--------------- ?????? UTC ?????? ?????? (??????)
  *ptrPvd->startVector.utcTime->month = t->tm_mon+1;    // (INPUT) <--------------- ?????? UTC ?????? ?????? (??????)
  *ptrPvd->startVector.utcTime->day = t->tm_mday;       // (INPUT) <--------------- ?????? UTC ?????? ?????? (??????)
  *ptrPvd->startVector.utcTime->hour = t->tm_hour;      // (INPUT) <--------------- ?????? UTC ?????? ?????? (??????)
  *ptrPvd->startVector.utcTime->minute = t->tm_min;     // (INPUT) <--------------- ?????? UTC ?????? ?????? (??????)
  *ptrPvd->startVector.utcTime->second = t->tm_sec;     // (INPUT) <--------------- ?????? UTC ?????? ?????? (??????)

  ptrPvd->startVector.elevation = new DSRC_Elevation_t;
  ptrPvd->startVector.heading = new Heading_t;
  ptrPvd->startVector.speed = new (struct TransmissionAndSpeed);
  ptrPvd->startVector.posAccuracy = NULL;     // OPTIONAL, not to use
  ptrPvd->startVector.posConfidence = NULL;   // OPTIONAL, not to use
  ptrPvd->startVector.timeConfidence = NULL;  // OPTIONAL, not to use
  ptrPvd->startVector.speedConfidence = NULL; // OPTIONAL, not to use

  ptrPvd->startVector.Long = Vli_.longitude;                // (INPUT) <--------------- ?????? ????????? ?????? (??????) (Longitude, DD ?????????)
  ptrPvd->startVector.lat = Vli_.latitude;                  // (INPUT) <--------------- ?????? ????????? ?????? (??????) (Latitude,  DD ?????????)
  *ptrPvd->startVector.elevation = Vli_.elevation;          // (INPUT) <--------------- ?????? ????????? ?????? (??????) (Elevation)   
  *ptrPvd->startVector.heading = Vli_.heading;              // (INPUT) <--------------- ?????? ????????? ?????? ?????? (?????? 0???)           
  ptrPvd->startVector.speed->speed = Vli_.speed;            // (INPUT) <--------------- ?????? ????????? ??????        
  ptrPvd->startVector.speed->transmisson = Vli_.transmisson;// (INPUT) <--------------- ?????? ????????? ????????? ??????          

  ptrPvd->vehicleType.hpmsType = new VehicleType_t;
  ptrPvd->vehicleType.keyType = NULL;       // OPTIONAL, not to use
  ptrPvd->vehicleType.fuelType = NULL;      // OPTIONAL, not to use
  ptrPvd->vehicleType.iso3883 = NULL;       // OPTIONAL, not to use
  ptrPvd->vehicleType.regional = NULL;      // OPTIONAL, not to use
  ptrPvd->vehicleType.responderType = NULL; // OPTIONAL, not to use
  ptrPvd->vehicleType.responseEquip = NULL; // OPTIONAL, not to use
  ptrPvd->vehicleType.role = NULL;          // OPTIONAL, not to use
  ptrPvd->vehicleType.vehicleType = NULL;   // OPTIONAL, not to use
  *ptrPvd->vehicleType.hpmsType = VehicleType_car; 

  ptrPvd->snapshots.list.count = 1; 
  ptrPvd->snapshots.list.array = new (struct Snapshot *);
  ptrPvd->snapshots.list.array[0] = new (struct Snapshot);
  struct Snapshot *ptrSnapshot = ptrPvd->snapshots.list.array[0]; 

  ptrSnapshot->thePosition.utcTime = new (struct DDateTime);
  ptrSnapshot->thePosition.utcTime->year = new DYear_t;
  ptrSnapshot->thePosition.utcTime->month = new DMonth_t;
  ptrSnapshot->thePosition.utcTime->day = new DDay_t;
  ptrSnapshot->thePosition.utcTime->hour = new DHour_t;
  ptrSnapshot->thePosition.utcTime->minute = new DMinute_t;
  ptrSnapshot->thePosition.utcTime->second = new DSecond_t;
  ptrSnapshot->thePosition.utcTime->offset = NULL; // OPTIONAL, not to use

  ptrSnapshot->thePosition.elevation = new DSRC_Elevation_t;
  ptrSnapshot->thePosition.speed = new (struct TransmissionAndSpeed);
  ptrSnapshot->thePosition.heading = new Heading_t;
  ptrSnapshot->thePosition.posAccuracy = NULL;     // OPTIONAL, not to use
  ptrSnapshot->thePosition.posConfidence = NULL;   // OPTIONAL, not to use
  ptrSnapshot->thePosition.timeConfidence = NULL;  // OPTIONAL, not to use
  ptrSnapshot->thePosition.speedConfidence = NULL; // OPTIONAL, not to use

  *ptrSnapshot->thePosition.utcTime->year = t->tm_year+1900; // (INPUT) <--------------- ?????? ????????? PVD??? UTC ?????? ?????? (??????)
  *ptrSnapshot->thePosition.utcTime->month = t->tm_mon+1;    // (INPUT) <--------------- ?????? ????????? PVD??? UTC ?????? ?????? (???)
  *ptrSnapshot->thePosition.utcTime->day = t->tm_mday;       // (INPUT) <--------------- ?????? ????????? PVD??? UTC ?????? ?????? (???)
  *ptrSnapshot->thePosition.utcTime->hour = t->tm_hour;      // (INPUT) <--------------- ?????? ????????? PVD??? UTC ?????? ?????? (???)
  *ptrSnapshot->thePosition.utcTime->minute = t->tm_min;     // (INPUT) <--------------- ?????? ????????? PVD??? UTC ?????? ?????? (???)
  *ptrSnapshot->thePosition.utcTime->second = t->tm_sec;     // (INPUT) <--------------- ?????? ????????? PVD??? UTC ?????? ?????? (???)
  
  ptrSnapshot->thePosition.lat = Vli_.latitude;                   // (INPUT) <--------------- ?????? ????????? ?????? (??????) (Longitude, DD ?????????)
  ptrSnapshot->thePosition.Long = Vli_.longitude;                 // (INPUT) <--------------- ?????? ????????? ?????? (??????) (Latitude,  DD ?????????) 
  *ptrSnapshot->thePosition.elevation = Vli_.elevation;           // (INPUT) <--------------- ?????? ????????? ?????? (??????) (Elevation)   
  *ptrSnapshot->thePosition.heading = Vli_.heading;               // (INPUT) <--------------- ?????? ????????? ?????? ?????? (?????? 0???)               
  ptrSnapshot->thePosition.speed->speed = Vli_.speed;             // (INPUT) <-------------- -?????? ????????? ??????                  
  ptrSnapshot->thePosition.speed->transmisson = Vli_.transmisson; // (INPUT) <--------------- ?????? ????????? ????????? ??????          

  return 0;
}

/* DEBUG */
void IchthusV2X::gen_PvdMsg()
{
  pvd_msg.msg_type = 5;
}

int IchthusV2X::parse_pvd(MessageFrame_t *pvd)
{
  ProbeVehicleData_t *ptrPvd = &pvd->value.choice.ProbeVehicleData;

  printf("ProbeID\n");
  printf("  id\n");
  printf("    entityID    : %02X:%02X:%02X:%02X\n", ptrPvd->probeID->id->choice.entityID.buf[0],
                                                    ptrPvd->probeID->id->choice.entityID.buf[1],
                                                    ptrPvd->probeID->id->choice.entityID.buf[2],
                                                    ptrPvd->probeID->id->choice.entityID.buf[3]);
  printf("startVector\n");
  printf("  utcTime\n");
  printf("    year        : %ld\n", *ptrPvd->startVector.utcTime->year);
  printf("    month       : %ld\n", *ptrPvd->startVector.utcTime->month);
  printf("    day         : %ld\n", *ptrPvd->startVector.utcTime->day);
  printf("    hour        : %ld\n", *ptrPvd->startVector.utcTime->hour);
  printf("    minute      : %ld\n", *ptrPvd->startVector.utcTime->minute);
  printf("    second      : %ld\n", *ptrPvd->startVector.utcTime->second);
  printf("  Long      : %ld\n", ptrPvd->startVector.Long);
  printf("  lat       : %ld\n", ptrPvd->startVector.lat);
  printf("  elevation : %ld\n", *ptrPvd->startVector.elevation);
  printf("  heading   : %ld\n", *ptrPvd->startVector.heading);
  printf("  speed\n");
  printf("    speed       : %ld\n", ptrPvd->startVector.speed->speed);
  
  printf("    transmisson : ");
  if (ptrPvd->startVector.speed->transmisson == 0)
  {
    printf("Neutral");
  }
  else if (ptrPvd->startVector.speed->transmisson == 1)
  {
    printf("Park");
  }
  else if (ptrPvd->startVector.speed->transmisson == 2)
  {
    printf("Forward gears");
  }
  else if (ptrPvd->startVector.speed->transmisson == 3)
  {
    printf("Reverse gears");
  }
  printf("(%ld)\n", ptrPvd->startVector.speed->transmisson);

  printf("vehicleTiype\n");
  switch(*ptrPvd->vehicleType.hpmsType)
  {
    case e_VehicleType::VehicleType_car:
      printf("  hpmsType  : VehicleType_car(4)\n");
  }
  
  for (int i = 0; i < ptrPvd->snapshots.list.count; i++)
  {
    struct Snapshot *ptrSnapshot = ptrPvd->snapshots.list.array[i];

    printf("snapshots[%d]\n", i);
    printf("  thePosition\n");
    printf("    utcTime\n");
    printf("      year   : %ld\n", *ptrSnapshot->thePosition.utcTime->year);
    printf("      month  : %ld\n", *ptrSnapshot->thePosition.utcTime->month);
    printf("      day    : %ld\n", *ptrSnapshot->thePosition.utcTime->day);
    printf("      hour   : %ld\n", *ptrSnapshot->thePosition.utcTime->hour);
    printf("      minute : %ld\n", *ptrSnapshot->thePosition.utcTime->minute);
    printf("      second : %ld\n", *ptrSnapshot->thePosition.utcTime->second); 
    printf("  Long      : %ld\n", ptrSnapshot->thePosition.Long);
    printf("  lat       : %ld\n", ptrSnapshot->thePosition.lat);
    printf("  elevation : %ld\n", *ptrSnapshot->thePosition.elevation);
    printf("  heading   : %ld\n", *ptrSnapshot->thePosition.heading);
    printf("  speed\n");
    printf("    speed       : %ld\n", ptrSnapshot->thePosition.speed->speed);
    printf("    transmisson : ");
    if (ptrSnapshot->thePosition.speed->transmisson == 0)
    {
      printf("Neutral");
    }
    else if (ptrSnapshot->thePosition.speed->transmisson == 1)
    {
      printf("Park");
    }
    else if (ptrSnapshot->thePosition.speed->transmisson == 2)
    {
      printf("Forward gears");
    }
    else if (ptrSnapshot->thePosition.speed->transmisson == 3)
    {
      printf("Reverse gears");
    }
    printf("(%ld)\n", ptrSnapshot->thePosition.speed->transmisson);  
  }

  return 0;
}

int IchthusV2X::encode_j2735_uper(char *dst, MessageFrame_t *src)
{
  // int res = -1;

  asn_enc_rval_t ret = uper_encode_to_buffer(&asn_DEF_MessageFrame,
                                              NULL,
                                              src,
                                              dst, MAX_UPER_SIZE);

  /* UPER Encoding Success */ 
  if (ret.encoded > 0)
  {
    return ret.encoded;
  }
  /* UPER Encoding Failed */
  else
  { 
    if (ret.failed_type != NULL)
    {
      std::cout << "encoded error value name = " << ret.failed_type->name << std::endl;
    }
    return -1;
  }
}

int IchthusV2X::request_tx_wave_obu(char *uper, unsigned short uperLength)
{
  if(sockFd < 0)
  {
    return -1;
  }
  int packetLen = uperLength + sizeof(struct CestObuUperPacketHeader);

  char packet[OBU_RECEIVE_BUFFER_SIZE];  // tcp header size + uper binary size 

  struct CestObuUperPacketHeader *ptrHeader = (struct CestObuUperPacketHeader *)&packet[0];
  ptrHeader->messageType = 0x4311; // TX_WAVE_UPER
  ptrHeader->seq = packetSeq++;
  ptrHeader->payloadLen = uperLength;
  ptrHeader->deviceType = 0xCE;
  memcpy(ptrHeader->deviceId,clientDeviceId,3);

  memcpy(packet + sizeof(struct CestObuUperPacketHeader), uper, uperLength);

  int res = write(sockFd, packet, packetLen);

  /* PVD Hz DEBUG */
  time_t timer1;
  struct tm *t1;
  timer1 = time(NULL);
  RCLCPP_INFO(this->get_logger(), "send time : %ld\n",timer1);

  if (res > 0)
  {
    printf("TX - \"TX_WAVE_UPER\" SEQ[%d] = ", ptrHeader->seq);
    print_hex(uper, uperLength);

    if (res != packetLen)
    {            
      std::cout << "DEBUG tcp tx purge" << std::endl;
      return -1;
    }
    else
    {
      return res;
    }
  }

  return 0;
}

void IchthusV2X::sigint_handler(int sig)
{  
	// fprintf(stdout,"done !\n");
  std::cout << "done!" << std::endl;
  done = true;
}

void IchthusV2X::install_signal_handler(void)
{
  struct sigaction sa; 

  memset(&sa, 0, sizeof sa); 
  sa.sa_handler = sigint_handler;

  if (sigaction(SIGINT, &sa, NULL) < 0) {
    // fprintf(stderr,"ctrl+c sigint install failed \n");
    std::cout << "ctrl+c sigint install failed" << std::endl;
    exit(-1);
  }
  // fprintf(stdout,"Press <Ctrl-C> to exit\n");
  std::cout << "Press <Ctrl-C> to exit" << std::endl;
}

}

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ichthus_v2x::IchthusV2X>();
  
  rclcpp::Rate rate(5);
  while(rclcpp::ok())
  {
    rclcpp::spin_some(node);
    rate.sleep();
  }

  return 0;
}