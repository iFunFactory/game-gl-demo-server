// PLEASE ADD YOUR EVENT HANDLER DECLARATIONS HERE.

#include <boost/bind.hpp>
#include <funapi.h>
#include <gflags/gflags.h>

#include "event_handlers.h"
#include "gamelift_demo_object.h"

#include <funapi/network/gamelift_sdk_manager.h>
#include <funapi/network/gamelift_client_manager.h>

#include <aws/core/Aws.h>

// TODO: move these to MANIFEST.json
DEFINE_string(field_alias, "alias-6ac3be1d-9746-4823-8db7-c2eb05f0ff64", "field alias");
DEFINE_string(field2_alias, "alias-037ac3f1-5af3-464d-a5a8-9438e257d9db", "field 2 alias");
DEFINE_string(instance_alias, "alias-cfd3344b-53ed-4837-bf00-2dbee9c6b493", "instance alias");

namespace {

static Aws::SDKOptions options;

class GameliftDemoServer : public Component {
 public:
  static bool Install(const ArgumentMap &arguments) {
    LOG(INFO) << "Built using Engine version: " << FUNAPI_BUILD_IDENTIFIER;
    gamelift_demo::ObjectModelInit();
    gamelift_demo::RegisterEventHandlers();

    Aws::InitAPI(options);
    GameLiftClientManagerMap = new std::map<std::string, GameLiftClientManager*>();

    GameLiftClientManager *instanceManager = new GameLiftClientManager(FLAGS_instance_alias);
    instanceManager->SetUpAwsClient(Aws::Region::AP_NORTHEAST_2);
    GameLiftClientManagerMap->insert(std::pair<string, GameLiftClientManager*>("instance", instanceManager));

    GameLiftClientManager *fieldManager = new GameLiftClientManager(FLAGS_field_alias);
    fieldManager->SetUpAwsClient(Aws::Region::AP_NORTHEAST_2);
    GameLiftClientManagerMap->insert(std::pair<string, GameLiftClientManager*>("field", fieldManager));

    GameLiftClientManager *field2Manager = new GameLiftClientManager(FLAGS_field2_alias);
    field2Manager->SetUpAwsClient(Aws::Region::AP_NORTHEAST_2);
    GameLiftClientManagerMap->insert(std::pair<string, GameLiftClientManager*>("field2", field2Manager));

    return true;
  }

  static bool Start() {
    return true;
  }

  static bool Uninstall() {
    Aws::ShutdownAPI(options);
    GGameLiftSDKManager->FinalizeGameLift();
    return true;
  }
};

}  // unnamed namespace


REGISTER_STARTABLE_COMPONENT(GameliftDemoServer, GameliftDemoServer)
