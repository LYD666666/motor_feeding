 #include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "weight.h"
#include "motor.h"
#include "math.h"
#include <stdio.h>


//投食
//#define touch1_debug 1
#define touch2_debug 1

//卡料——debug
#define key_debug 1

//阀门与推杆电机驱动 （电机驱动器）
#define valve 14
#define push1 4
#define push2 13
#define pwmct 25

#define PWMA 25  //控制电机1 PWM控制引脚
#define freq 1000      //PWM波形频率5KHZ
#define pwm_Channel_1  0 //使用PWM的通道0
#define resolution  10    //使用PWM占空比的分辨率，占空比最大可写2^10-1=1023
#define interrupt_time_control 15//定时器15ms中断控制时间

void motor_init(void);
void Set_Pwm(int moto1);
void posetive_motor(int arg);
void sendADC(void); //Task1 称重并发送云平台任务
void pushrod(void *pvParameter); //Task2 推杆状态发送云任务
void weight_request(void *pvPrameter); 
void callback(char *topic, byte *payload, unsigned int length); //mqtt回调
void whenisfinish(void);
void ResetWeight(void);

int a = 1;
long count;
long count0;
long count1 = 400;
char string1[10];
char msgJson[75];                       //要发送的json格式的数据
char msg_buf[200];                      //发送信息缓冲区

#ifdef touch1_debug
  char dataWeight[] = "{\"castweight\":%ld}"; //信息模板
  char datastausR [] = "{\"castweight\":%ld,\"caststaus\":\"ready\"}";
  char datastausB [] = "{\"castweight\":%ld,\"caststaus\":\"busy\"}";
  char datastausF [] = "{\"castweight\":%ld,\"caststaus\":\"finish\"}";
  
  char situate_ready[] = "{\"situate\":\"ready\",\"stepname\":\"/casting_publish\"}";
  char situate_busy[] = "{\"situate\":\"busy\",\"stepname\":\"/casting_publish\"}";
  char situate_finish[] = "{\"situate\":\"finish\",\"stepname\":\"/casting_publish\"}";
  char castbegin1 [] = "{\"castbegin\":%s}";  
  
  char stoptransport[] = "{\"motorswitch\":\"off\"}";
  char stopfeeding[] = "{\"planid\":\"1\"}";
  char rst[] = "{\"planid\":\"1\",\"feedstate\":\"ready\"}";
#endif

#ifdef touch2_debug
  char dataWeight[] = "{\"castweight\":%ld}"; //信息模板
  char datastausR [] = "{\"castweight\":%ld,\"caststaus\":\"ready\"}";
  char datastausB [] = "{\"castweight\":%ld,\"caststaus\":\"busy\"}";
  char datastausF [] = "{\"castweight\":%ld,\"caststaus\":\"finish\"}";
  
  char situate_ready[] = "{\"situate\":\"ready\",\"stepname\":\"/casting_publish_1\"}";
  char situate_busy[] = "{\"situate\":\"busy\",\"stepname\":\"/casting_publish_1\"}";
  char situate_finish[] = "{\"situate\":\"finish\",\"stepname\":\"/casting_publish_1\"}";
  char castbegin1 [] = "{\"castbegin\":%s}";  
  
  char stoptransport[] = "{\"motorswitch\":\"off\"}";
  char stopfeeding[] = "{\"planid\":\"1\"}";
  char rst[] = "{\"planid\":\"1\",\"feedstate\":\"ready\"}";
#endif

unsigned short json_len = 4;            // json长度
long weight_num[2];   //存储滤波
long weight;          //总量
long weight_Notyet;   //待投量
long weight_default = 15;  //默认量
long weight_real;  //真实投喂量
long weight_revise; //想修改的投喂量
long weight_fin;    //投料变化值记录
int castbegin_num;
int connectNum = 0;
long count_A = 0;

// WiFi
const char *ssid = "iQOO3";         // Enter your WiFi name      STOP607_2.4  iQOO3       TP_LINK_FF67
const char *password = "asdfghjkl";    // Enter WiFi password    sigmaWYU3601 asdfghjkl   abc123456

// MQTT Broker
const char *mqtt_broker = "114.132.163.111";
#ifdef touch1_debug
  const char *topic = "/casting_publish";         /*向云平台定阅*/
  const char *topic1 = "/casting_subscribe";      /*向云平台发送*/
#endif

#ifdef touch2_debug
  const char *topic = "/casting_publish_1";         /*向云平台定阅*/
  const char *topic1 = "/casting_subscribe_1";      /*向云平台发送*/
#endif

const char *topic2 = "/situate";                /*发送设备状态*/
const char *topic3 = "/stopfeeding";            /*总体投喂停止*/
const char *topic4 = "/rst";                    /*总体投喂复位*/
const char *topic5 = "/feeding_publish";        /*送料复位*/
const char *mqtt_username = "cwl";
const char *mqtt_password = "19260817";
const int mqtt_port = 1883; 

//开关阀门状态标志位
int flag ;

//开关阀门状态标志位
int list_flag = 0;


//实例对象
WiFiClient espClient;
PubSubClient client(espClient);

//  1   硬件初始化
void HarewardInit(void)
{
  // Set software serial baud to 115200;
  Serial.begin(115200); //串口

  // Set ADC GPIO
  pinMode(HX_DT, INPUT);
  pinMode(HX_SCk, OUTPUT);

  //motor _push
  motor_init();

  // connecting to a WiFi network
  WiFi.begin(ssid, password);
  while (!WiFi.isConnected())
  {
    connectNum++;
    delay(500);
    Serial.println("Connecting to WiFi..");
    if(connectNum>=5)
    {
      ESP.restart();
      }
  }
  Serial.println("Connected to the WiFi network");

  // connecting to a mqtt broker
  client.setServer(mqtt_broker, mqtt_port);   //MQTT服务器连接函数（服务器IP，端口号）
  client.setCallback(callback);               //设定回调方式，当ESP32收到订阅消息时会调用此方法
  while (!client.connected())
  {
    #ifdef touch1_debug
    String client_id = "motor_feeding";
    #endif

    #ifdef touch2_debug
    String client_id = "motor_feeding_1";
    #endif
    
    //client_id += String(WiFi.macAddress());
    Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
    if (client.connect(client_id.c_str(), mqtt_username, mqtt_password))
    {
      Serial.println("Public emqx mqtt broker connected");
    }
    else
    {
      Serial.print("failed with state ");
      Serial.print(client.state());
      vTaskDelay(100/portTICK_PERIOD_MS); //需要更改，最后用RTOS
    }
  }

  //    1   推杆、阀门复位，都是关闭状态,发送ready，准备称重
  digitalWrite(valve,LOW);
  Set_Pwm(500);
  delay(7000);
  Serial.print("now is reset\r\n");
  Set_Pwm(0);
  delay(500);

    // ADC转换(获取count0的值用于调平衡)
    ReadCount();
    count0 = 0;
    for (int i = 0; i < 8; i++)
    count0 += ReadCount();
    count0 /= 8;

    client.subscribe(topic);                 //连接MQTT服务器后订阅主题
    snprintf(msgJson, 80, datastausR, weight_real); //将称重数据套入dataWeight模板中, 生成的字符串传给msgJson
    client.publish(topic1,msgJson);       //发送一个ready
    client.publish(topic2,situate_ready);
    
}

//  2    阀门打开，开始称重并发送busy
void feeding1(void)
{
   digitalWrite(valve,HIGH);
   //delay(3000);
   snprintf(msgJson, 80, datastausB, weight_real); //将称重数据套入dataWeight模板中, 生成的字符串传给msgJson5
   client.publish(topic2,situate_busy);
   //client.publish(topic1,msgJson);
}


// 5 获取投喂真实量
void tasktomotor(void)
{
      //打开
      //断开NC,关阀门
      digitalWrite(valve,LOW); 
      delay(4000);
      count = ReadCount() - count0;
      count = ((count - count1)/3000);

      weight_real = count;            //这时的count为真实量 //因为阀门关上并稳定了，不会有料的进来，设置的值一般在一半也不会挤压
      weight = weight - weight_real;  //总量-真实量=下次待投量
      weight_Notyet = weight;

      Serial.print("1_real_weight:");
      Serial.println(weight_real);
      Serial.print("2_real_weight:");
      Serial.println(weight_Notyet);

        snprintf(msgJson, 80, datastausB, weight_real); //将称重数据套入dataWeight模板中, 生成的字符串传给msgJson
        json_len = strlen(msgJson);               // msgJson的长度
        client.publish(topic1, msgJson); 

      whenisfinish();
      ResetWeight(); //投料和复位重量
  }

// 6 判断投喂是否最后一次
void whenisfinish (void)
{
   if(weight_Notyet < weight_default&&weight_Notyet>0)
   {
      weight_default = weight_Notyet;

      
    }
    else if (weight_Notyet<=0)
    {
            a = 0;
            weight = 0;   //数据清空
            flag = 1;
            list_flag = 1;
            castbegin_num = 2;  //off
            
            digitalWrite(valve,LOW);
            delay(1000);
            /*最后一次投喂*/
            //client.publish(topic4, rst);
            //client.publish(topic2,situate_finish);
            
          
      }
      else delay(50);
  }
  

// 7 投料和复位重量
void ResetWeight(void)
{
      Set_Pwm(-1000); //下滑推杆开始投料
      delay(4000);  //等待漏料
      Set_Pwm(0);
       
  
    //关闭
      Set_Pwm(1000);
      delay(4000);
      Set_Pwm(0);

    if(flag == 0)
    {
      digitalWrite(valve,HIGH); 
      delay(5000);
    }
       count0 = 0;
       for (int i = 0; i < 8; i++)
       count0 += ReadCount();
       count0 /= 8;
       snprintf(msgJson, 80, datastausR, weight_real); //将称重数据套入dataWeight模板中, 生成的字符串传给msgJson
      //client.publish(topic1,msgJson);

       if(list_flag == 1)
        {
          //client.publish(topic5, stoptransport);
          //client.publish(topic3, stopfeeding);   //停止链条
          client.publish(topic2,situate_finish);
    
          delay(30);
          list_flag = 0;
         }
  }
  
// 4 Task1  json格式传输  反馈一次重量
void sendADC(void)
{
    if (client.connected())
    {
       for(int i=0;i<2;i++){
 
        String id;
        long count = ReadCount() - count0;
        count = ((count - count1)/3000);
        weight_num[i] = count;
        Serial.print("numm:");
        Serial.println(count);
        
        delay(200); //需要更改，最后用RTOS   一延时就过去执行别的任务
      }
      count = weight_num[1]-weight_num[0];
       Serial.print("fin:");
      Serial.println(count);
      if(count > weight_default) 
      {
         tasktomotor();
       }
      else if (count > 0)
      {
        count_A += count;
        Serial.print("count_A:");
        Serial.println(count_A);
        }

        if(count_A > weight_default)
        {
          count_A = 0;
          tasktomotor();
          }
    }
}

//  3  回调函数 读取总重量
void callback(char *topic, byte *payload, unsigned int length)
{
  char json[200];
  char *parameter = (char *)malloc(length); 
   const char *castbegin = (char *)malloc(length);
  /* 解析JSON */
  StaticJsonDocument<200> jsonBuffer2; //声明一个JsonDocument对象，长度200
  
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  for (int i = 0; i < length; i++)
  {
    //Serial.print((char)payload[i]);
    parameter[i] = (char)payload[i];
    Serial.print(parameter[i]);
  } 
  
  // 反序列化JSON
  DeserializationError error = deserializeJson(jsonBuffer2, parameter,length);
     if (error) 
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    JsonObject dataWeight = jsonBuffer2.as<JsonObject>();

   if(strncmp(parameter,"castweight",strlen("castweight"))==0)
  {
    if(weight == 0){
       weight = dataWeight[String("castweight")];              // 读取整形数据,总重量
       flag = 1;
      }
  }
     if(strncmp(parameter,"castbegin",strlen("castbegin"))==0)
  {
    castbegin = dataWeight["castbegin"];
  }
  
  Serial.print("now:\r\n");
  Serial.println(weight);
  Serial.println(dataWeight);
  if(strncmp(parameter,"castweight",strlen("castweight"))!=0)
  {
    Serial.println(castbegin);
    if(strncmp(castbegin,"on",strlen(castbegin))== 0)
    {
      castbegin_num = 1;
      }
     else if(strncmp(castbegin,"off",strlen(castbegin))== 0)
     {
       castbegin_num = 2;
      }
  }
  free(parameter);
  Serial.println("--------------------------");
}


//初始化
void setup()
{
  //联网和硬件设备初始化
  HarewardInit();

  Serial.print("init OK\r\n");
}


/*-----------------------------------------------------------*/


//循环
void loop()
{
  client.loop();
  if(castbegin_num==1)
  {
    snprintf(msgJson, 80, datastausB, weight_real); //将称重数据套入dataWeight模板中, 生成的字符串传给msgJson5

    //client.publish(topic1,msgJson);
    if(flag == 1)                                   //第一次循环需要阀门关闭状态打开阀门，结束后的第二次也要打开阀门发送busy
    {
      client.publish(topic2,situate_busy);
      feeding1();
      flag = 0;
    }
  
      sendADC();
      delay(50);
    }
  else if(castbegin_num == 2)                      //如果云平台发送off就会开在关闭无法运行，要云平台发on才能运行
  {
    if(a == 1)
    {
      client.publish(topic2,situate_ready);
      a = 0;
    }
    digitalWrite(valve,LOW);
    delay(1000);
    Serial.println("off");
  }
}