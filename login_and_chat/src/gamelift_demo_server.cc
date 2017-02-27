// PLEASE ADD YOUR EVENT HANDLER DECLARATIONS HERE.

#include <boost/bind.hpp>
#include <funapi.h>
#include <gflags/gflags.h>

#include "event_handlers.h"
#include "gamelift_demo_object.h"

#include <aws/core/Aws.h>
#include "gamelift_client_manager.h"

DECLARE_string(app_flavor);

DEFINE_string(field1_alias, "alias-6ac3be1d-9746-4823-8db7-c2eb05f0ff64", "field1 fleet alias");
DEFINE_string(field2_alias, "alias-037ac3f1-5af3-464d-a5a8-9438e257d9db", "field2 fleet alias");

namespace {

static Aws::SDKOptions options;

class GameliftDemoServer : public Component {
 public:
  static bool Install(const ArgumentMap &arguments) {
    LOG(INFO) << "Built using Engine version: " << FUNAPI_BUILD_IDENTIFIER;
    gamelift_demo::ObjectModelInit();
    gamelift_demo::RegisterEventHandlers();
    return true;
  }

  static bool Start() {
    if (FLAGS_app_flavor == "login") {
     Aws::InitAPI(options);
     Field1GameliftManager = new GameLiftManager(FLAGS_field1_alias);
     Field1GameliftManager->SetUpAwsClient(Aws::Region::AP_NORTHEAST_2);
     Field2GameliftManager = new GameLiftManager(FLAGS_field2_alias);
     Field2GameliftManager->SetUpAwsClient(Aws::Region::AP_NORTHEAST_2);
    }
    return true;
  }

  static bool Uninstall() {
    if (FLAGS_app_flavor == "login") {
      Aws::ShutdownAPI(options);
    }
    return true;
  }
};

}  // unnamed namespace


REGISTER_STARTABLE_COMPONENT(GameliftDemoServer, GameliftDemoServer)
