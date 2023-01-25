/*************************************************************************************/
/* File: gnmi_client.cc
/*
/* Description: Implements client based on gNMI.proto
/*               => Subscribe Request (to get streaming data) against user inputted prefix/path
/*               => Async Get Request against user inputted prefix/path
/*
/* Copyright 2016 Ciena. All Rights Reserved.
/*
/* CONFIDENTIALITY AND LIMITED USE
/*
/* This software, including any software of third parties embodied herein,
/* contains information and concepts which are confidential to Ciena
/* and such third parties. This software is licensed for use
/* solely in accordance with the terms and conditions of the applicable
/* license agreement with Ciena or its authorized distributor.
/*************************************************************************************/


#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/create_channel.h>



#if !defined(GPR_GRPC_REL_0_13_0) && !defined(GPR_GRPC_REL_01_0_0)
#error Please define stack in Makefile
#endif

#define GRPC_SECURE

#ifdef GPR_GRPC_REL_0_10_1
#include <grpc++/credentials.h>
#endif

#ifdef GPR_GRPC_REL_0_13_0
#include <grpc++/security/credentials.h>
#include <core/support/env.h>
#endif

#ifdef GPR_GRPC_REL_01_0_0
#include <grpc++/security/credentials.h>
#include <grpc++/support/channel_arguments.h>
#include <core/lib/support/env.h>
#endif

#include "gNMI.grpc.pb.h"
#include "gNMI.pb.h"

#include <iomanip>

#include <google/protobuf/util/json_util.h>

using std::setw;
using std::left;


using grpc::Channel;
using grpc::ChannelArguments;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using grpc::WriteOptions;
using grpc::SslCredentialsOptions;
using grpc::ChannelCredentials;

using gnmi::gNMI;
using gnmi::GetRequest;
using gnmi::GetResponse;
using gnmi::SetRequest;
using gnmi::SetResponse;
using gnmi::SubscribeRequest;
using gnmi::SubscribeResponse;



#define STREAM_SERVER_PORT "10161"
#define ASYNC_SERVER_PORT  "10161"
#define SLASH_CHAR         '/'


char  usrNamPasswdStr[200];     
char *userid, *password; 
int requestInLoop = 0;
int cmdDelay = 0;
int64_t timeout_value=10; // in seconds


char *client_key;
char *client_cert;

int  get_client_credentials(char* clientKey, char* clientCert){

 long fsize;

 FILE *fp_key  = fopen(clientKey, "rb");
 FILE *fp_cert = fopen(clientCert, "rb");
 if (fp_key == NULL || fp_cert == NULL) {
    return 0;
 }


 /*Read whole key file */
 fseek(fp_key, 0, SEEK_END);
 fsize = ftell(fp_key);
 fseek(fp_key, 0, SEEK_SET);
 if (fsize != -1)
 {   
     fseek(fp_key, 0, SEEK_SET);
     client_key = (char*)malloc(fsize * sizeof(char));
     if (client_key == NULL)
     {
         printf ("Malloc Failed for key\n");
         fclose(fp_key);
         return 0;
      }
      fread(client_key, fsize, 1, fp_key);
      fclose(fp_key);
      client_key[fsize] = 0;
 }
 else
 {
    printf ("Couldn't calculate file size:ftell failed for key\n");
    fclose(fp_key);
    return 0;
 }


 /*Read whole cert file */
 fsize = 0;
 fseek(fp_cert, 0, SEEK_END);
 fsize = ftell(fp_cert);
 fseek(fp_cert, 0, SEEK_SET);
 if(fsize != -1)
 {
     fseek(fp_cert, 0, SEEK_SET);
     client_cert = (char*)malloc(fsize * sizeof(char));
     if (client_cert == NULL)
     {
         printf("Malloc Failed for cert\n");
         fclose(fp_cert);
         return 0;
     }
     fread(client_cert, fsize, 1, fp_cert);
     fclose(fp_cert);
     client_cert[fsize] = 0;
     return 1;
 }
 else
 {
     printf("Couldn't calculate file size:ftell failed for cert\n");
     fclose(fp_cert);
     return 0;
 }

}

static  void usage( )
{
    printf ("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf ("Usage : gnmi_client -t <rpc_type> -l <usr-name:passwd> [-s] -i <host_ip_addr> \n");
    printf ("                 [-d <portNo>] [-h <httpPortNo>] [-c <delay>] [-m <mode>] [-g <sample_interval>]\n");
    printf ("                 { [ -x <prefix>] OR  [-p <path> -p <path> -p <path> ....] }\n\n");
    printf (" -t <rpc_type>            : 1 => Async Get Request, 2=> Sync/Streaming request\n");
    printf ("                          : 3 => Async Set Request\n");
    printf (" -l <usr-name:passwd>     : Login credentials\n");
    printf (" -s                       : Have secure channel\n");    
    printf (" -i <hostIpAddr>          : IP of 6500 box where gRPC server is running.\n");
    printf (" -w <dcnPort>             : DCN port; which is optional, default DCN port is 10161\n");
    printf (" -h <httpPort>            : httpPort; which is optional, default http port is 443\n");
    printf (" -m <mode>                : 0 => STREAM, 1=> ONCE\n");
    printf (" -g <sample_interval>     : Multiple of 10 secs pre R1285/1510 OR equivalent nanoseconds R1285/1510 onwards\n");
    printf (" -G <sample_interval>     : Multiple of 10 secs recommended for R1285/1510 onwards\n");
    printf (" -x <prefix>              : Prefix\n");
    printf (" -p <path>                : Path for Sync/Async Get rpc\n");
    printf (" -d <path>                : Delete Path for Async Set request (NOT SUPPORTED)\n");        
    printf (" -r <path>:<value>        : Replace Path for Async Set reuqest (NOT SUPPORTED)\n");
    printf (" -u <path>:<value>        : Update Path for Async Set reuest\n");
    printf (" -c <delay>               : Send Async Get request in loop against given delay\n");
    printf (" -S <max-msg-size>        : Channel max message size value in MB (default value is 20MB)\n");
    printf ("                            If received msg size > 20MB then client will exit with\n");
    printf ("                            rpc ststus code 13 => INTERNAL\n");    
    printf (" -T <timeout>             : SSL handshake timeout(minval needed is 4 sec; default value is 10s)\n");
    printf ("                            If tiemout is not enough then client will exit with Security handshake failed\n");
    printf ("                            and returned rpc status code will be 14 => UNAVAILABLE\n");
    printf (" -K <file path>           : User key file\n");
    printf (" -C <file path>           : User certificate file\n\n");

    printf ("Note:\n");
    printf ("  RPC Error codes:\n");
    printf ("    3 => INVALID ARGUMENT  : \n");
    printf ("    5 => NOT FOUND         : \n");    
    printf ("    7 => PERMISSION DENIED : \n");    
    printf ("    8 => RESOURCE_EXHAUSTED: Max Streaming/Async Client limit(=5/10)reached.\n");
    printf ("                             Disconnect one client, wait for 10s for server \n");    
    printf ("                             to clear the entry & try again.\n");    
    printf ("   10 => ABORTED           : Session killed at server\n");    
    printf ("   12 => UNIMPLEMENTED     : RPC not implemented\n");    
    printf ("   13 => INTERNAL          : Something is wrong or broken on server/client\n");
    printf ("   14 => UNAVAILABLE       : Data not available/Invalid path/Login server times out\n");    
    printf ("   16 => UNAUTHENTICATED   : Login credentials not OK\n");
    printf ("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

    exit(0);

}

static const char* errorCodeToStr(::grpc::StatusCode statusCode)
{
    switch(statusCode)
    {
        case ::grpc::StatusCode::OK:                 return("OK");        
        case ::grpc::StatusCode::INVALID_ARGUMENT:   return("INVALID_ARGUMENT");
        case ::grpc::StatusCode::NOT_FOUND:          return("NOT_FOUND");
        case ::grpc::StatusCode::PERMISSION_DENIED:  return("PERMISSION_DENIED");
        case ::grpc::StatusCode::RESOURCE_EXHAUSTED: return("RESOURCE_EXHAUSTED");
        case ::grpc::StatusCode::ABORTED:            return("ABORTED");
        case ::grpc::StatusCode::UNIMPLEMENTED:      return("UNIMPLEMENTED");
        case ::grpc::StatusCode::INTERNAL:           return("INTERNAL"); 
        case ::grpc::StatusCode::UNAVAILABLE:        return("UNAVAILABLE");
        case ::grpc::StatusCode::UNAUTHENTICATED:    return("UNAUTHENTICATED");
        default: return("UNKNOWN");
    }
}
static const char *getUserName()
{
  uid_t uid = geteuid();
  struct passwd *pw = getpwuid(uid);
  if (pw)
  {
    return pw->pw_name;
  }

  return "";
}

static void downloadCertAndSetEnv(char* hostIpStr, char* portStr, char* httpPortStr)
{ 
#ifdef GRPC_SECURE
    char setSecBuff[200];
    char fileBuff1[100];
    char fileBuff2[100];
    pid_t pid = getpid();
    struct stat file_stats;
    
    sprintf(fileBuff1, "/tmp/roots.pem_%s_%d", getUserName(), pid);
    sprintf(fileBuff2, "/tmp/roots.pem_%s_%d_temp", getUserName(), pid);
    
    while(1)
    {
        sprintf(setSecBuff, "echo -n | timeout 10s openssl s_client -connect %s:%s  &> %s", hostIpStr,httpPortStr, fileBuff2);
        system(setSecBuff);
        if(stat(fileBuff2, &file_stats) >=0 )
        {
            break;
        }
        printf("!");
        sleep(1);
    }
    
    printf("\nFixed");   
    sprintf(setSecBuff, "cat %s | sed -ne '/-BEGIN CERTIFICATE-/,/-END CERTIFICATE-/p' > %s", fileBuff2,fileBuff1);
    system(setSecBuff);
    
    setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH", fileBuff1, true);
    sprintf(setSecBuff, "chmod 777 %s", fileBuff1);
    system(setSecBuff);
#endif
}

static int  convertYangPath2ProtoPath(char* yangPath, ::gnmi::Path& path)
{
    
    int i;
    char* tmpPath;
    int strLen = strlen(yangPath);
    char* pathStart;
    int ret = 0;

    tmpPath = (char*)calloc(strLen+1,1);
    
    if(tmpPath == NULL || yangPath == NULL)
    {
        return (ret);
    }

    strncpy(tmpPath,yangPath,strLen);
 
    pathStart = (tmpPath);
    for (i=0; i < strLen; i++)
    {
        if( *(tmpPath+i) == SLASH_CHAR )
        {
            //printf("<%s\n",tmpPath+i);
            *(tmpPath+i) = 0;
            if(strlen(pathStart))
            {
                //printf(">>>>>>>>>>>>>>>>>>>>>>>>> %s\n",pathStart);
                path.add_element(pathStart);
            }
            pathStart = (tmpPath+i+1);
        }
    }
    if(strlen(pathStart))
    {
        //printf(">>>>>>>>>>>>>>>>>>>>>>>>=>%s\n",pathStart);
        path.add_element(pathStart);
    }
    
    ret = 1;
    free(tmpPath);
    return(ret);
}



// Async server client
class TelemAsyncClient {
 public:
  explicit TelemAsyncClient(std::shared_ptr<Channel> channel)
      : stub_(gNMI::NewStub(channel)) {}

  void OcfgAsyncGetResp(GetRequest& getReq) {
    GetResponse reply;
    ClientContext context;
    CompletionQueue cq;
    Status status;
    
    context.AddMetadata("username", userid);     
    context.AddMetadata("password", password);      
    
    
    // Print Populated GetRequest
    printGetRequest(&getReq); 
    std::unique_ptr<ClientAsyncResponseReader<GetResponse> > rpc(stub_->AsyncGet(&context, getReq, &cq));
    
    rpc->Finish(&reply, &status, (void*)1);
    
    void* got_tag;
    bool ok = false;
    cq.Next(&got_tag, &ok);
    
    //GPR_ASSERT(ok);
    GPR_ASSERT(got_tag == (void*)1);
    if(status.ok() || status.error_code() == 5) 
    {
        printGetResponse(&reply);
    } 
    std::cout << std::endl;
    std::cout <<  "RPC STATUS : " << status.error_code() << " => " << errorCodeToStr(status.error_code()) << std::endl;
    
  }

  void OcfgAsyncSetResp(SetRequest& setReq) {
    SetResponse reply;
    ClientContext context;
    CompletionQueue cq;
    Status status;
    
    context.AddMetadata("username", userid);
    context.AddMetadata("password", password);
    
    
    // Print Populated SetRequest
    printSetRequest(&setReq);
    std::unique_ptr<ClientAsyncResponseReader<SetResponse> > rpc(stub_->AsyncSet(&context, setReq, &cq));
    
    rpc->Finish(&reply, &status, (void*)1);
    
    void* got_tag;
    bool ok = false;
    cq.Next(&got_tag, &ok);
    
    //GPR_ASSERT(ok);
    GPR_ASSERT(got_tag == (void*)1);
    if (status.ok()) 
    {
       printSetResponse(&reply);
    } 
    std::cout << std::endl;
    std::cout <<  "RPC STATUS : " << status.error_code() << " => " << errorCodeToStr(status.error_code()) << std::endl;
  }

  void Run(GetRequest& getReq) 
  {
      while(1) 
      { // Make repeated client requests
           OcfgAsyncGetResp(getReq);
           if(!requestInLoop) break;
           sleep(cmdDelay);
      }
  }

  void RunSet(SetRequest& setReq) 
  {
      while(1) 
      { // Make repeated client requests
           OcfgAsyncSetResp(setReq);
           if(!requestInLoop) break;
           sleep(cmdDelay);
      }
  }
  
 private:
  std::unique_ptr<gNMI::Stub> stub_;
  std::shared_ptr<Channel> _channel;

  void printGetRequest(GetRequest* request)
  {

     ::gnmi::Notification notif;
     int  path_size;
     ::gnmi::Path prefix;
     ::std::string          element;
     int                        element_size;

     std::cout << std::endl << "AsyncGet(GetRquest) =>" << std::endl;	

     if(request->has_prefix())
     {

          prefix =  request->prefix();
          element_size = prefix.element_size();
          element.clear();
          for(int i=0; i < element_size; i++)
          {
            element.append("\"");            
            element.append(prefix.element ( i ));
            element.append("\" ");
          }
          std::cout << "  prefix : " << element << std::endl;
          
     }
     else
     {
           std::cout << "  prefix :  --" << std::endl;
     }

     path_size = request->path_size();

     if(!path_size)
       std::cout << "  Path[] : NA " <<  std::endl;
     
     for(int j=0; j < path_size; j++)
     {
       element_size = request->path(j).element_size();
       element.clear();
       if(!element_size)
           element.append("NA");
       for(int k=0; k < element_size; k++)
       {
           element.append("\"");
           element.append(request->path(j).element(k));
           element.append("\" ");
       }
       std::cout << "  Path[" << j << "]: "<<  element << std::endl;
     }

  }  

  void printSetRequest(SetRequest* request)
  {

     int                        path_size;
     int                        upd_size;
     int                        del_size;     
     ::gnmi::Path         prefix;
     
     ::gnmi::Update       upd;
     ::gnmi::Path         path;
     ::gnmi::Value        val;
     
     ::std::string              element;
     int                        element_size;

     std::cout << std::endl << "AsyncSet(SetRquest) =>" << std::endl;
     

     if(request->has_prefix())
     {

          prefix =  request->prefix();
          element_size = prefix.element_size();
          element.clear();
          for(int i=0; i < element_size; i++)
          {
            element.append("\"");
            element.append(prefix.element ( i ));
            element.append("\" ");
          }
          std::cout              << "  Prefix        : " << element << std::endl;
          
     }
     else
     {
          std::cout              << "  Prefix        : NA" << std::endl;
     }


     // delete
     del_size = request->delete__size();
     if(!del_size)
         std::cout              << "  Delete[]      : NA" << std::endl;
     
     for(int j=0; j < del_size; j++)
     {
         path = request->delete_(j);
         element_size = path.element_size();
         element.clear();
         for(int k=0; k < element_size; k++)
         {
           element.append("\"");            
           element.append(path.element ( k ));
           element.append("\" ");
         }
         std::cout              << "  Delete[" << j << "]     : " << element << std::endl;
     }
      
     // replace
     // update
     upd_size = request->update_size();
     if(!upd_size)
         std::cout              << "  Update[]      : NA" << std::endl;
     
     for(int j=0; j < upd_size; j++)
     {
         upd = request->update(j);
         std::cout              << "  Update[" << j << "]     : "<<  std::endl;
         if(upd.has_path())
         {
           path = upd.path();
           element_size = path.element_size();
           element.clear();
           for(int k=0; k < element_size; k++)
           {
             element.append("\"");            
             element.append(path.element ( k ));
             element.append("\" ");
           }
           std::cout              << "    Path        : " << element << std::endl;
           
         }
         else
         {
           std::cout              << "    Path        : NA" << std::endl;
         }
     
         std::cout              << "    Value(t/n/v): ";
         
         if(upd.has_value())
         {
           val = upd.value();
           //std::cout <<  val.type() << "/" << val.name()  << "/" << val.value() << std::endl;  
           std::cout <<  val.value() << std::endl;  
     
         }
         else
         {
           std::cout << "---" << std::endl;
         }
     }

  }  

  void printSetResponse(SetResponse* reply)
  {

      ::gnmi::UpdateResult         upd_resp;
      int                          resp_size;
      ::gnmi::Path                 path;
      
      resp_size = reply->response_size();
      
      std::cout << std::endl << "AsyncSet(SetResponse) =>" << std::endl;

      
      ::gnmi::Path       prefix;
      ::std::string      element;
      int                element_size;
      ::std::string      alias;
      int                update_size;
      long long          timestamp;
   
   
      if(reply->has_prefix())
      {
        prefix =  reply->prefix();
        element_size = prefix.element_size();
        element.clear();
        for(int i=0; i < element_size; i++)
        {
          element.append("\"");            
          element.append(prefix.element ( i ));
          element.append("\" ");
        }
        std::cout              << "  Prefix            : " << element << std::endl;
        
      }
      else
      {
        std::cout              << "  Prefix            : NA" << std::endl;
      }
              
      if(resp_size)
      {
   
        for( int a=0; a < resp_size; a++)
        {
        
          std::cout              << "  UpdateResponse[" << a << "] : " <<  std::endl;
          upd_resp = reply->response(a);

          // Operation
          std::cout              << "    Operation       : " << ::gnmi::UpdateResult_Operation_Name(upd_resp.op()) << std::endl;
          
          // TimeStamp
          timestamp = upd_resp.timestamp();
          std::cout              << "    Timestamp       : " << timestamp << std::endl;
          
          // Path
          if(upd_resp.has_path())
          {
            path = upd_resp.path();
            element_size = path.element_size();
            element.clear();
            for(int k=0; k < element_size; k++)
            {
              element.append("\"");
              element.append(path.element(k));
              element.append("\" ");
            }
            std::cout              << "    Path            : " << element << std::endl;
            
          }
          else
          {
            std::cout              << "    Path            : NA" << std::endl;
          }
          
          // Error
          if(upd_resp.has_message())
          {
              ::gnmi::Error error;
              error = upd_resp.message();
              std::cout              << "   Error code       : " <<  error.code() << " => " << errorCodeToStr((::grpc::StatusCode)error.code()) << std::endl;
              std::cout              << "         msg        : " <<  error.message() << std::endl; 
          }
          else
          {
              std::cout              << "   Error code       : NA" << std::endl;
              std::cout              << "         msg        : NA" << std::endl;
          }

        }


    }
    else
    {
      std::cout              << "  UpdateResponse[]  : NA" <<  std::endl;
    }
    
    // SetResponse->Error
    if(reply->has_message())
    {
      ::gnmi::Error error;
      error = reply->message();
      std::cout              << "  Overall" << std::endl;
      std::cout              << "   Error code       : " <<  error.code() << " => " << errorCodeToStr((::grpc::StatusCode)error.code()) << std::endl;
      std::cout              << "         msg        : " <<  error.message() << std::endl; 
    }
    else
    {
      std::cout              << "  Overall" << std::endl;
      std::cout              << "   Error code       : NA" << std::endl;
      std::cout              << "         msg        : NA" << std::endl;
    }
    
  
  }
  void printGetResponse(GetResponse* reply)
  {

      ::google::protobuf::util::JsonOptions  options;
      std::string                            output;

      ::gnmi::Notification notif;
      int  notification_size;
      notification_size = reply->notification_size();

      std::cout << std::endl << "AsyncGet(GetResponse) =>" << std::endl;	      
      if(notification_size)
      {
        ::gnmi::Path prefix;
        ::std::string      element;
        int                element_size;
        ::std::string      alias;
        int                update_size;
        long long          timestamp;

        for( int a=0; a < notification_size; a++)
        {
          std::cout << " --------------------------------------" <<  std::endl;
          std::cout << " Notification[" << a << "]: " <<  std::endl;
          notif = reply->notification(a);
        
          timestamp = notif.timestamp();
          std::cout << "  timestamp      : " << timestamp << std::endl;

          if(notif.has_prefix())
          {

            prefix =  notif.prefix();
            element_size = prefix.element_size();
            element.clear();
            for(int i=0; i < element_size; i++)
            {
              element.append("\"");            
              element.append(prefix.element ( i ));
              element.append("\" ");
            }
            std::cout << "  prefix         : " << element << std::endl;
          }
          else
          {
              std::cout << "  prefix         : --" << std::endl;
          }

          //alias = notif.alias();
          //std::cout << "  alias : " << alias << std::endl;

          update_size = notif.update_size();
            
          ::gnmi::Update upd;
          ::gnmi::Path   path;
          ::gnmi::Value  val;

          if(!update_size)
            std::cout << "  Update[] : NA " <<  std::endl;
            
            
          for(int j=0; j < update_size; j++)
          {
            upd = notif.update(j);
            std::cout << "  Update[" << std::setfill('0') << std::setw(3) << j << "]    : " <<  std::endl;
            if(upd.has_path())
            {
              path = upd.path();
              element_size = path.element_size();
              element.clear();
              for(int k=0; k < element_size; k++)
              {
                element.append("\"");            
                element.append(path.element ( k ));
                element.append("\" ");
              }
              std::cout << "    Path         : " << element << std::endl;
            }
            else
            {
              std::cout << "    Path         : NA" <<  std::endl;
            }

            std::cout << "    Value(t/v)   : ";
            if(upd.has_value())
            {
              val = upd.value();
              //std::cout <<  val.type() << "/" << val.name()  << "/" << val.value() << std::endl;  
              std::cout <<  val.value() << std::endl;
            }
            else
            {
              std::cout << "---" << std::endl;
            }
            
          }

        }
    }
    else
    {
        std::cout << " Notification[]  : NA" <<  std::endl;
    }
      
    if(reply->has_error())
    {
        ::gnmi::Error error;
        error = reply->error();
        std::cout << " Error code      : " <<  error.code() << " => " << errorCodeToStr((::grpc::StatusCode)error.code()) << std::endl;
        std::cout << "       msg       : " <<  error.message() << std::endl; 
    }
    else
    {
        std::cout << " Error code      : NA" <<  std::endl;
        std::cout << "       msg       : NA" <<  std::endl;
    }

  }  

};

// Streaming server client
// Blocks the RPC, since the server will continuously stream data
// unless client disconnects or session terminated at server.
// Subscribe() RPC :
//    Blocks as server contiously streams data every 10 seconds

class TelemSyncClient 
{
 public:
  explicit TelemSyncClient(std::shared_ptr<Channel> channel)
      : stub_(gNMI::NewStub(channel)) {}

  int TelemSubscribe(SubscribeRequest& subsReq)
  {
    SubscribeResponse  telem_data;
    WriteOptions       wrOpts;
    int                ret =1;

    ClientContext context;

    context.AddMetadata("username", userid);     
    context.AddMetadata("password", password);  

    auto streamReaderWrter = stub_->Subscribe(&context);

    printSubsRequest(subsReq);
        
    streamReaderWrter->Write(subsReq, wrOpts);
    
    while(streamReaderWrter->Read(&telem_data))
    {
        ret = printSubcribeResponse(&telem_data);
        if(!ret) break;
    }

    Status status = streamReaderWrter->Finish();

    if(status.error_code() != ::grpc::StatusCode::OK)
        printSubcribeResponse(&telem_data);
    
    std::cout << std::endl;
    std::cout << "RPC STATUS : " << status.error_code() << " => " << errorCodeToStr(status.error_code()) << std::endl;
    return (ret);
    
  }

  void TelemGet( )
  {
                             
    ClientContext context;
    ::gnmi::GetRequest     request;
    ::gnmi::GetResponse    response;
    std::string user("Sync OME6500");

    // Populate GetRequest
    ::gnmi::Path         *prefix  = new ::gnmi::Path();
    prefix->add_element(user);
    request.set_allocated_prefix(prefix);
        
    Status status = stub_->Get(&context, request, &response);
    
    if (!status.ok()) 
    {
      std::cout << "Get rpc failed : " << status.error_code() << std::endl;
      return;
    }

    printSyncGetResponse(&response);
    return;
  }

  void Run(int isSync, SubscribeRequest& subsReq) 
  {
       while (1) 
       {
         if ( isSync )
         {
            if(!TelemSubscribe(subsReq)) break;  // This is a blocking call - should never return!
                                                 // If it does the error condition should be handled 
                                                 // in RPC context. Please see class description above.
             sleep(4);
         }
         else
         {
             TelemGet(); // Make repeated client requests
             sleep(1);
          }
       }
  }
  
  
 private:

  std::unique_ptr<gNMI::Stub> stub_;

  
  void printSyncGetResponse(GetResponse* reply)
  {

      ::gnmi::Notification notif;
      int notification_size;
      notification_size = reply->notification_size();
      
      if(notification_size)
      {
        ::gnmi::Path prefix;
        ::std::string          element;
        int                        element_size;
        ::std::string          alias;
        int                        update_size;
        long long               timestamp;
        
        std::cout << std::endl << "AsyncGet(GetResponse) =>" << std::endl;	

        for( int a=0; a < notification_size; a++)
        {
        
          std::cout << " Notification[" << a << "]: " <<  std::endl;
          notif = reply->notification(a);
        
          timestamp = notif.timestamp();
          std::cout << "  timestamp : " << timestamp << std::endl;

          if(notif.has_prefix())
          {
            prefix =  notif.prefix();
            element_size = prefix.element_size();
            element.clear();
            
            for(int i=0; i < element_size; i++)
            {
              element.append("\"");
              element.append(prefix.element ( i ));
              element.append("\" ");
            }
            std::cout << "  prefix : " << element << std::endl;
          }
          else
          {
              std::cout << "  prefix : --" << std::endl;
          }

          alias = notif.alias();
          std::cout << "  alias : " << alias << std::endl;

          update_size = notif.update_size();
            
          ::gnmi::Update upd;
          ::gnmi::Path   path;
          ::gnmi::Value  val;

          if(!update_size)
            std::cout << "  Update[] : NA " <<  std::endl;
          for(int j=0; j < update_size; j++)
          {
            upd = notif.update(j);
            std::cout << "  Update[" << j << "]: "<<  std::endl;
            if(upd.has_path())
            {
              path = upd.path();
              element_size = path.element_size();
              element.clear();
              for(int k=0; k < element_size; k++)
              {
                element.append("\"");
                element.append(path.element ( k ));
                element.append("\" ");
                
              }
              std::cout << "    Path :" << element << std::endl;
              
            }
            else
            {
            }

            std::cout << "    Value   value/type/name : ";  
            if(upd.has_value())
            {
              val = upd.value();
              std::cout << val.value() << std::endl;  
            
            }
            else
            {
              std::cout << "---" << std::endl;
            }
            
          }

        }
    }
    else
    {
      std::cout << " Notification[ ] : NA" <<  std::endl;
    }

  };  

  void printSubsRequest(SubscribeRequest& subsReq)
  {

     int                path_size;
     int                subs_size;     
     ::gnmi::Path prefix;
     ::std::string      element;
     int                element_size;

     std::cout << std::endl << "SubscribeRequest =>" << std::endl;
     std::cout <<                    "  SubscriptionList"        << std::endl;
     if(subsReq.subscribe().has_prefix())
     {

          prefix =  subsReq.subscribe().prefix();
          element_size = prefix.element_size();

          element.clear();
          for(int i=0; i < element_size; i++)
          {

             element.append("\"");            
             element.append(prefix.element ( i ));
             element.append("\" ");
          }
          std::cout << "    Prefix : " << element << std::endl;
          
     }
     else
     {
           std::cout << "    Prefix :  --" << std::endl;
     }
     
     subs_size = subsReq.subscribe().subscription_size();
     
     for(int j=0; j < subs_size; j++)
     {
       std::cout << "    Subscription[" << j << "]: "<<  std::endl;
       if(subsReq.subscribe().subscription(j).has_path())
       {
       
           element_size = subsReq.subscribe().subscription(j).path().element_size();
           element.clear();
           if(!element_size)
               element.append("NA");
           for(int k=0; k < element_size; k++)
           {
               element.append("\"");
               element.append(subsReq.subscribe().subscription(j).path().element(k));
               element.append("\" ");
           }
           std::cout << "      Path: "<<  element << std::endl;
        }
        else
        {
           std::cout << "      Path: NA"<<  std::endl;
        }
       
     }

  }  
  
  int printSubcribeResponse(SubscribeResponse*  telem_data)
  {
      ::gnmi::Notification notif;
      ::std::string              element;      

      if(telem_data->sync_response() == true)
      {
          std::cout << "  SyncResponse    : " << telem_data->sync_response() << std::endl;
          return 0;
      }
      std::cout << std::endl << "SubscribeResponse:" << std::endl;
      if(telem_data->has_update())
      {
        ::gnmi::Path       prefix;
        int                element_size;
        ::std::string      alias;
        int                update_size;
        long long          timestamp;
        
        notif = telem_data->update();
        std::cout                    << "  Notification:" << std::endl;
        
        timestamp = notif.timestamp();
        std::cout << "    Timestamp     : " << timestamp << std::endl;
        if(notif.has_prefix())
        {

          prefix =  notif.prefix();
          element_size = prefix.element_size();
          
          element.clear();
          for(int i=0; i < element_size; i++)
          {
            element.append("\"");
            element.append(prefix.element ( i ));
            element.append("\" ");
          }
          std::cout << "    Prefix        : " << element << std::endl;
        }
        else
        {
            std::cout << "    Prefix        : --" <<  std::endl;
        }

        //alias = notif.alias();
        //std::cout << "    Alias : " << alias << std::endl;

        update_size = notif.update_size();
          
        ::gnmi::Update upd;
        ::gnmi::Path   path;
        ::gnmi::Value  val;

        if(!update_size)
          std::cout << "    Update[ ] : NA " <<  std::endl;
        
        for(int j=0; j < update_size; j++)
        {
          upd = notif.update(j);
          std::cout << "    Update[" << j << "]: "<<  std::endl;
          if(upd.has_path())
          {
            path = upd.path();
            element_size = path.element_size();

            element.clear();
            for(int k=0; k < element_size; k++)
            {
              element.append("\"");
              element.append(path.element(k));
              element.append("\" ");
            }
            std::cout << "      Path        : " << element << std::endl;

              
          }
          else
          {
            std::cout << "      Path        : --" << std::endl;
          }
          
          if(upd.has_value())
          {
            val = upd.value();
            std::cout << "      Value t/v   : " << val.value() << std::endl;
          }
          else
          {
            std::cout << "      Value v/t   : --" << std::endl;  
          }
            
        }
        
        if(notif.delete__size())
        {
            for(int i=0; i < notif.delete__size(); i++)
            {
                element.clear();
                if(notif.delete_(i).element_size())
                {
                    for(int j=0; j < notif.delete_(i).element_size(); j++)
                    {
                        element.append("\"");
                        element.append(notif.delete_(i).element (j));
                        element.append("\" ");
                    }
                    std::cout << "    Delete        : " << element << std::endl;
                }
            }
        }
        else
        {
           std::cout << "    Delete        : NA" << std::endl;
        }
        
      } 
      else
      {
          std::cout << "  Notification    : NA" << std::endl;
      }
      
      if(telem_data->has_error())
      {
          ::gnmi::Error error;
          error = telem_data->error();
          
          std::cout << "  Error code      : " <<  error.code() << " => " << errorCodeToStr((::grpc::StatusCode)error.code()) << std::endl;
          std::cout << "        msg       : " <<  error.message() << std::endl; 
          
      }
      else
      {
          std::cout << "  Error code      : NA" <<  std::endl;
          std::cout << "        msg       : NA" <<  std::endl;
      }
      

      return 1;
  }
  

};  

/*
 Extract Attribute and it's Value from the first pair in the given source string
    srcStr   = aa1:vv1,aa2:vv2
    paramStr = aa1
    valStr   = vv1
*/
void extractParamAndValue(char* srcStr, char* paramStr, char* valStr )
{

    char* srcPtr = srcStr;
    int leaf_list = 0;

    while(*srcPtr != 0 && *srcPtr != ':' )
    {
        if(*srcPtr != 32)
          *paramStr++ = *srcPtr;

        srcPtr++;
    }
    
    if(*srcPtr == ':') 
    {
        srcPtr++;
        if(*srcPtr == '[')
            leaf_list = 1;
        
        while(*srcPtr != 0 )
        {
            /*if(!leaf_list && *srcPtr == ',')
                break;*/
                
            //if(*srcPtr != 32)
              *valStr++ = *srcPtr++;
  
            //srcPtr++;
            
            if(*srcPtr == ']')
              leaf_list = 0;
            
        }
    }
    
}
    
int main(int argc, char** argv) {

    extern char *optarg;
    int  opt;
    int  rpcType=0;
    char cmd[20];
    char*cmd_ptr;
    char hostIpStr[50];
    char ipStr[20];
    char httpPortStr[10];
    char portStr[10];
    char clientKey[200];
    char clientCert[200];    
    int  prefixPresent = 0;
    int  updatePresent = 0;    
    int  pathPresent = 0;    
    int  hostIpPresent = 0;
    int  isSecure = 0;
    int  isCredentials = 0;
    char* colon = NULL;
    char* portPos1 = NULL;
    char* portPos2 = NULL;   
    int   mode = 0;
    uint64_t   sampleInterval = 0;
    char  rmRootCertBuff[100];
    int   channelMaxMsgSize = 20;
    
    GetRequest       getReq;
    SetRequest       setReq;    
    SubscribeRequest subsReq;
 
    memset(clientKey, 0, 200);
    memset(clientCert, 0, 200);
    
    memset(httpPortStr,0,10);
    strcpy(httpPortStr, "10161");
       
    memset(portStr,0,10);
    strcpy(portStr, STREAM_SERVER_PORT);

    sprintf(rmRootCertBuff, "rm -f /tmp/roots.pem_%s_%d*", getUserName(),getpid());
    
    ::gnmi::SubscriptionList   *subscribe = new ::gnmi::SubscriptionList();    

    cmd_ptr = cmd;

    strcpy(cmd_ptr, (char*)argv[0]);
    if(cmd[0] = '.' && cmd[1] == '/')
    {
      cmd_ptr = cmd_ptr + 2;
    }
    
    while((opt=getopt(argc, argv, "t:sl:i:x:p:d:r:u:c:w:h:m:g:K:C:S:T:G:")) != -1)
    {
        switch(opt) 
        {
            case 't':  // rpc type 
                rpcType = atoi(optarg);
                if( rpcType <= 0 || rpcType > 3)
                {
                    printf("\n\nInvalid rpc type !!! \n\n");
                    usage();
                }
                break;

            case 'c':  // request in loop with given delay
                cmdDelay = atoi(optarg);
                
                if( cmdDelay < 0 || cmdDelay > 10)
                {
                    printf("\n\nValid delay range [0 > <delay> < 10 ]!!! \n\n");
                    usage();
                }
                requestInLoop = 1;
                break;
            case 'l':  // for secure channel
                strncpy (usrNamPasswdStr, optarg,200);

                colon = strstr(usrNamPasswdStr,":");

                if(colon == NULL)
                {
                    usage();
                }
                *colon = 0;
                userid = usrNamPasswdStr; 
                password = colon+1; 
                isCredentials = 1;
                break;

            case 's':  // for secure channel
                isSecure = 1;
                break;

            case 'i':
                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                memset(hostIpStr,0,50);
                strncpy(hostIpStr, optarg,50-1);
                
                hostIpPresent = 1;
                break;

            case 'w':
                memset(portStr,0,10);
                strncpy(portStr, optarg, 10-1);
                break;
                
            case 'h':
                memset(httpPortStr,0,10);
                strncpy(httpPortStr, optarg, 10-1);
                break;

            case 'm':
                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                if( rpcType != 2)
                {
                    printf("\n\nOption not valid for rpc type = %d !!! \n\n",rpcType);
                    usage();
                }
                mode = atoi(optarg);
                subscribe->set_mode((::gnmi::SubscriptionList_Mode)mode);
                break;

            case 'g':
                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                else if( rpcType != 2)
                {
                    printf("\n\nOption not valid for rpc type = %d !!! \n\n",rpcType);
                    usage();
                }
                else if(pathPresent)
                {
                    printf("\n\nsample interval must be entered before path !!! \n\n");
                    usage();
                }
                    else if(0 != sampleInterval) //already filled by -G option.
                {
                    printf("\n\nChoose either -g(Pre R1285/R1510) or -G(R1285/1510 onwards) JA-403953!!! \n\n");
                    usage();
                }
                sampleInterval = atol(optarg);
                
                break;             
 
            case 'G':
                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                else if( rpcType != 2)
                {
                    printf("\n\nOption not valid for rpc type = %d !!! \n\n",rpcType);
                    usage();
                }
                else if(pathPresent)
                {
                    printf("\n\nsample interval must be entered before path !!! \n\n");
                    usage();
                }
                else if(0 != sampleInterval) //already filled by -g option.
                {
                    printf("\n\nChoose either -g(Pre R1285/R1510) or -G(R1285/1510 onwards) JA-403953!!! \n\n");
                    usage();
                }
                sampleInterval = atol(optarg);
                sampleInterval = sampleInterval * 1000000000;
                
                break;              
                
            case 'x':
                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                if(!prefixPresent)
                {
                    if(updatePresent == 1)
                    {
                        printf ("\n\nCan't have prefix after update !!!\n");
                        usage();
                    }
                    char* pathStr = (char*)calloc(1,strlen(optarg)+1);
                    strcpy (pathStr, optarg);

                    // Fill in the prefix
                    if( rpcType ==1)
                    {//AsyncGet
                        ::gnmi::Path* prefix = new ::gnmi::Path; 
                        convertYangPath2ProtoPath(pathStr, *prefix);
                        getReq.set_allocated_prefix(prefix);
                    }
                    else if( rpcType==3)
                    {//AsyncSet
                        ::gnmi::Path* prefix = new ::gnmi::Path; 
                        convertYangPath2ProtoPath(pathStr, *prefix);
                        setReq.set_allocated_prefix(prefix);
                    }
                    else
                    {//Sync
                    
                        ::gnmi::Path* prefix = new ::gnmi::Path; 
                        convertYangPath2ProtoPath(pathStr, *prefix);
                        subscribe->set_allocated_prefix(prefix);
                    }
                        
                    prefixPresent =1;
                    free(pathStr);
                }
                else
                {
                    printf ("\n\nCan't have more than 1 prefix !!!\n");
                    usage();
                }
                break;
                    
            case 'p':
            {
                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                if(rpcType > 2)
                {
                    printf("\n\n Invalid option -p as rpc type = 3 !!! \n\n");
                    usage();
                }

                char* pathStr = (char*)calloc(1,strlen(optarg)+1);
                strcpy (pathStr, optarg);
                
                // Fill in the path
                if(rpcType == 1)
                { //AsyncGet

                    ::gnmi::Path* path;
                    path = getReq.add_path();
                    convertYangPath2ProtoPath(pathStr, *path);
                }
                else
                {// Sync
                    
                    ::gnmi::Path          *path= new ::gnmi::Path();
                    ::gnmi::Subscription  *subscription ; 
 
                    convertYangPath2ProtoPath(pathStr, *path);
                    subscription = subscribe->add_subscription();
                    subscription->set_allocated_path(path);
                    subscription->set_sample_interval(sampleInterval);
                    
                }
                pathPresent = 1;
                free(pathStr);
                break;
            }
            case 'u':
            case 'r':         
            {

                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                if(rpcType != 3)
                {
                    printf("\n\n Invalid option -u as rpc type != 3 !!! \n\n");
                    usage();
                }
                
                char* pathStr = (char*)calloc(1,strlen(optarg)+1);
                strcpy (pathStr, optarg);
                {//AsyncSet
                    int paramLen = strlen(optarg) + 1;
                    int valLen   = strlen(optarg) + 1;
                    
                    char* paramStr = (char*)calloc(1,paramLen);
                    char* valStr   = (char*)calloc(1,valLen);
                    char* startPtr = NULL,*temp=NULL;
                    temp=strchr(pathStr,':');
                    if(temp)
                    {
                      *temp=0;
                      startPtr = strrchr(pathStr,'/');
                      if(startPtr==NULL)
                        startPtr=pathStr;
                      *temp=':';
                    }
                    if(startPtr != NULL)
                    {
                        *startPtr = 0;
                        startPtr = startPtr + 1;
                        extractParamAndValue(startPtr, paramStr, valStr);
                    }
                    else
                    {
                        startPtr = pathStr;
                    }
                    
                    while(startPtr != NULL)
                    {
                        ::gnmi::Path   *path;
                        ::gnmi::Value  *value;
                        ::gnmi::Update* update;
                    
                        path  = new ::gnmi::Path();
                        value = new ::gnmi::Value();
                        
                        if(opt == 'u')
                        {
                            update = setReq.add_update();
                            updatePresent  = 1;
                        }
                        else
                            update = setReq.add_replace();
                        
                        convertYangPath2ProtoPath(pathStr, *path);
                        if(*paramStr != 0)
                        {
                            path->add_element(paramStr);
                        }
                        
                        update->set_allocated_path(path);

                        //if(*valStr != 0 )
                        {
                            value->set_value((const char*)valStr);
                        }
                        
                        update->set_allocated_value(value);

                        startPtr = strchr(startPtr,',');
                        if(startPtr)
                        {
                            startPtr = strchr(startPtr,']');
                            if(startPtr)
                                startPtr = strchr(startPtr,',');
                        }
                            
                        if(startPtr != NULL)
                        {
                            startPtr = startPtr + 1;
                            memset(paramStr,0,paramLen);
                            memset(valStr,0,valLen);
                            extractParamAndValue(startPtr, paramStr, valStr);
                        }
                        
                    }                        
                    
                    free(paramStr);
                    free(valStr);
                }
                pathPresent = 1;
                free(pathStr);
                break;
            }    
            case 'd':
            {
                if( !rpcType )
                {
                    printf("\n\nrpc type must be entered first !!! \n\n");
                    usage();
                }
                if(rpcType != 3)
                {
                    printf("\n\n Invalid option -d as rpc type != 3 !!! \n\n");
                    usage();
                }
                char* pathStr = (char*)calloc(1,strlen(optarg)+1);
                strcpy (pathStr, optarg);
                {//AsyncSet
                    ::gnmi::Path *deletePath;
                    deletePath = setReq.add_delete_();
                    convertYangPath2ProtoPath(pathStr, *deletePath);
                }
                pathPresent = 1;
                free(pathStr);
                break;
            }
            case 'K' :
            {
                strcpy(clientKey,optarg);
                break;
            }
            case 'C' :
            {
                strcpy(clientCert,optarg);
                break;
            }
            
            case 'S' :
            {
                channelMaxMsgSize = atoi(optarg);
                if( channelMaxMsgSize <= 0  || channelMaxMsgSize > 100)
                {
                    printf("\n\n Invalid max message size %d!!! \n\n", channelMaxMsgSize);
                    usage();
                }
                break;
            }
            
            case 'T' :
            {
                timeout_value  = atoi(optarg);
                if(timeout_value <= 0)
                {
                    printf("\n\n Invalid timeout_value %d!!! \n\n", timeout_value);
                    usage();
                }
                break;
            }
            
            default:
                usage();
        }
    }

#ifdef GRPC_SECURE
    SslCredentialsOptions ssl_opts;
    ChannelArguments      args;
    
    args.SetSslTargetNameOverride("Ciena"); //This signifies common name present in X509 certificate
    args.SetInt(GRPC_ARG_MAX_MESSAGE_LENGTH,channelMaxMsgSize*1024*1024);
    /*update path for client credentias & shared objects */
    get_client_credentials(clientKey, clientCert);
    ssl_opts = {"", client_key, client_cert};
    if (clientKey[0] && clientCert[0])
    {
        if ((get_client_credentials(clientKey, clientCert)) && 
           (client_key != NULL) && (client_cert != NULL))
        {
            ssl_opts = {"", client_key, client_cert};
        }
        else
        {
            printf("Failure in reading cert/key pair\n");
            return 0;
        }
    }
    else
    {
        ssl_opts = {"", "", ""};
    }
#endif


    // The server on 6500 is built without openssl support for now.
    // Thus the client channels are passed arguments: InsecureCredentials()
    if(rpcType == 1 || rpcType == 3) /////////////////// ASYNC GET/SET
    {
        if( (!prefixPresent && !pathPresent) || !hostIpPresent || !isCredentials)
        {
            usage();
        }
        downloadCertAndSetEnv(hostIpStr, portStr, httpPortStr); 
        strcat(hostIpStr,":");
        strcat(hostIpStr,portStr);
        printf("Connecting to IP:%s, httpPort:%s\n",hostIpStr,httpPortStr);
        
        if(!isSecure) ///// INSECURE
        {
#ifdef GPR_GRPC_REL_0_10_1
            TelemAsyncClient telemAsyncClient(grpc::CreateChannel(std::string(hostIpStr), grpc::InsecureCredentials(), ChannelArguments()));
#endif
#if defined(GPR_GRPC_REL_0_13_0) || defined(GPR_GRPC_REL_01_0_0) 
            TelemAsyncClient telemAsyncClient(grpc::CreateChannel(std::string(hostIpStr), grpc::InsecureChannelCredentials()));
#endif
            if(rpcType == 1)
                telemAsyncClient.Run(getReq);
            else
                telemAsyncClient.RunSet(setReq);
        }
#ifdef GRPC_SECURE
        else
        { ////////////////// SECURE 
#ifdef GPR_GRPC_REL_0_10_1        
            TelemAsyncClient telemAsyncClient(grpc::CreateChannel(std::string(hostIpStr), grpc::SslCredentials(ssl_opts), ChannelArguments()));
#endif
#if defined(GPR_GRPC_REL_0_13_0) || defined(GPR_GRPC_REL_01_0_0) 
            TelemAsyncClient telemAsyncClient(CreateCustomChannel(std::string(hostIpStr), grpc::SslCredentials(ssl_opts), args));
#endif
            system(rmRootCertBuff);
            if(rpcType == 1)
                telemAsyncClient.Run(getReq);
            else
                telemAsyncClient.RunSet(setReq);
        }
#endif        
    }
    else if(rpcType == 2) ////////////////////////// SYNC 
    {
        if( ( !prefixPresent && !pathPresent) || !hostIpPresent || !isCredentials)
        {
            usage();    
        }
        downloadCertAndSetEnv(hostIpStr,portStr, httpPortStr);   
        strcat(hostIpStr,":");
        strcat(hostIpStr,portStr);
        
        printf("Connecting to IP:%s, httpPort:%s\n",hostIpStr,httpPortStr);
        if(!isSecure) ////////////// INSECURE         
        {
#ifdef GPR_GRPC_REL_0_10_1
            TelemSyncClient telemSyncClient(grpc::CreateChannel(std::string(hostIpStr), grpc::InsecureCredentials(), ChannelArguments()));
#endif
#if defined(GPR_GRPC_REL_0_13_0) || defined(GPR_GRPC_REL_01_0_0) 
            TelemSyncClient telemSyncClient(grpc::CreateChannel(std::string(hostIpStr), grpc::InsecureChannelCredentials()));
#endif
            subsReq.set_allocated_subscribe(subscribe);
            telemSyncClient.Run(1, subsReq);
        }
#ifdef GRPC_SECURE
        else ///////////////////////// SECURE 
        {
#ifdef GPR_GRPC_REL_0_10_1        
            TelemSyncClient telemSyncClient(grpc::CreateChannel(std::string(hostIpStr), grpc::SslCredentials(ssl_opts), ChannelArguments()));
#endif            
#if defined(GPR_GRPC_REL_0_13_0) || defined(GPR_GRPC_REL_01_0_0) 
            TelemSyncClient telemSyncClient(CreateCustomChannel(std::string(hostIpStr),grpc::SslCredentials(ssl_opts), args));
#endif
            subsReq.set_allocated_subscribe(subscribe);
            system(rmRootCertBuff);
            telemSyncClient.Run(1, subsReq);
        }
#endif        
    }
    else
    {
        usage();
    }
    return 0;       
}

