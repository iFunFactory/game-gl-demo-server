// PLEASE ADD YOUR EVENT HANDLER DECLARATIONS HERE.

#include "event_handlers.h"
#include <boost/foreach.hpp>
#include <funapi.h>
#include <glog/logging.h>
#include "gamelift_demo_loggers.h"
#include "gamelift_demo_messages.pb.h"
#include <funapi/network/gamelift_sdk_manager.h>
#include <funapi/network/gamelift_client_manager.h>

DECLARE_string(app_flavor);

namespace gamelift_demo {

bool isPlayerComes;

void EndServerAfter5Sec() {
  Timer::ExpireAfter(WallClock::FromSec(5), [](const Timer::Id &, const WallClock::Value &) {
    raise(SIGTERM);
    //kill(getpid(),SIGINT);
  });
}

void CloseSessionAfter1Sec(const Ptr<Session> &session) {
  Timer::ExpireAfter(WallClock::FromSec(1), [session](const Timer::Id &, const WallClock::Value &) {
    session->Close();
  });
}

void OnSessionOpened(const Ptr<Session> &session) {
  logger::SessionOpened(to_string(session->id()), WallClock::Now());
}

void OnSessionClosed(const Ptr<Session> &session, SessionCloseReason reason) {
  logger::SessionClosed(to_string(session->id()), WallClock::Now());
  if (FLAGS_app_flavor == "field") {
    BroadcastMessage(session->id(), "remove", session->GetContext()["MyCharacter"]);
  }
  Json context = session->GetContext();
  if (context.HasAttribute("player_session_id")) {
    GGameLiftSDKManager->RemovePlayerSession(session->GetContext()["player_session_id"].GetString());
  }
  if (FLAGS_app_flavor == "instance") {
    EndServerAfter5Sec();
  }
}

void OnClientRedirected(const std::string &account_id, const Ptr<Session> &session, bool success, const std::string &extra_data) {
  if (success) {
    LOG(INFO) << "Client(session=" << session->id() << ", account=" << account_id << ") logged in (redirected)";
    Json response;
    Json context;
    if (context.FromString(extra_data)) {
      if (GGameLiftSDKManager->AcceptPlayerSession(context["player_session_id"].GetString())) {
        session->SetContext(context);
        response = context["MyCharacter"];
        response["server_type"] = FLAGS_app_flavor;
        session->SendMessage("welcome", response, kDefaultEncryption, kTcp);
        BroadcastMessage(session->id(), "new", context["MyCharacter"]);
        isPlayerComes = true;
        std::vector<Ptr<Session>> session_map;
        AccountManager::GetAllLocalSessions(&session_map);
        BOOST_FOREACH(Ptr<Session> &player_session, session_map) {
          if (player_session->IsOpened() && player_session->IsTransportAttached() && player_session->id() != session->id()) {
            Json new_character = player_session->GetContext()["MyCharacter"];
            session->SendMessage("new", new_character, kDefaultEncryption, kTcp);
          }
        }
      }
      else {
        response["msg"] = "Player session is not valid. Unknown error. Please try later.";
        session->SendMessage("error", response, kDefaultEncryption, kTcp);
        CloseSessionAfter1Sec(session);
      }
    } else {
      LOG(INFO) << "Json parse failed. Redirecting extra_data is not valid.";
      CloseSessionAfter1Sec(session);
    }
  } else {
    LOG(INFO) << "Redirecting token isn't valid. " << session->id() << ", account=" << account_id;
    CloseSessionAfter1Sec(session);
  }
}

void OnTcpTransportDetached(const Ptr<Session> &session) {
  if (FLAGS_app_flavor == "field") {
    Json character_info = session->GetContext()["MyCharacter"];
    LOG(INFO) << "OnTcpTransportDetached : " << character_info.ToString();
    Ptr<Character> character = Character::FetchByName(character_info["Name"].GetString());
    character->SetLevel(character_info["Level"].GetInteger());
    character->SetField(character_info["Field"].GetInteger());
    character->Setx(character_info["x"].GetDouble());
    character->Sety(character_info["y"].GetDouble());
    character->Setz(character_info["z"].GetDouble());
    character->Setry(character_info["ry"].GetDouble());
    character->Setv(character_info["v"].GetDouble());
    character->Seth(character_info["h"].GetDouble());
  }
}

void BroadcastMessage(SessionId senderSessionId, const string &message_type, Json &message) {
  std::vector<Ptr<Session>> session_map;
  AccountManager::GetAllLocalSessions(&session_map);
  BOOST_FOREACH(Ptr<Session> &player_session, session_map) {
    if (player_session->IsOpened() && player_session->IsTransportAttached() && player_session->id() != senderSessionId) {
      player_session->SendMessage(message_type, message, kDefaultEncryption, kTcp);
    }
  }
}

// TODO: optimization
void OnRelay(const Ptr<Session> &session, const Json &message) {
  Json broadcast_message(message);
  Json &character_info = session->GetContext()["MyCharacter"];
  broadcast_message["Name"] = character_info["Name"];
  BroadcastMessage(session->id(), "relay", broadcast_message);
  for (Json::AttributeIterator it = broadcast_message.AttributeBegin(); it != broadcast_message.AttributeEnd(); ++it) {
    if (character_info.HasAttribute(it->GetName())) {
      character_info[it->GetName()] = it->GetValue();
    }
  }
}

void CheckAndMoveToInstance(const Ptr<Session> &session, string game_session_id, GameLiftClientManager *instanceManager) {
  Json response;
  Aws::GameLift::Model::GameSession game_session;
  if (instanceManager->SearchGameSessions(game_session, game_session_id)) {
    if (game_session.GetStatus() == Aws::GameLift::Model::GameSessionStatus::ACTIVE) {
      Aws::GameLift::Model::PlayerSession player_session;
      Json character_info = session->GetContext()["MyCharacter"];
      if (not instanceManager->CreatePlayerSession(game_session_id, character_info["Name"].GetString(), player_session)) {
        response["msg"] = "'CreatePlayerSession' failed.";
      } else {
        Rpc::PeerMap servers;
        size_t peer_count = Rpc::GetPeersWithTag(&servers, game_session_id);
        if (peer_count == 0) {
          response["msg"] = "There is no available instance server.";
        } else {
          BroadcastMessage(session->id(), "remove", session->GetContext()["MyCharacter"]);
          session->GetContext()["player_session_id"] = player_session.GetPlayerSessionId();
          if (AccountManager::RedirectClient(session, servers.begin()->first, session->GetContext().ToString())) {
            return;
          }
          else {
            response["msg"] = "Redirecting failed.";
          }
        }
      }
    } else {
      Timer::ExpireAfter(WallClock::FromMsec(200), [session, game_session_id, instanceManager](const Timer::Id &, const WallClock::Value &) {
        CheckAndMoveToInstance(session, game_session_id, instanceManager);
      });
      return;
    }
  } else {
    response["msg"] = "Cannot find created instance server. Unknown error, Please try later.";
  }
  session->SendMessage("error", response, kDefaultEncryption, kTcp);
}

void OnDungeon(const Ptr<Session> &session, const Json &message) {
  LOG(INFO) << "'dungeon' message recved.";
  GameLiftClientManager *instanceManager = GameLiftClientManagerMap->find("instance")->second;
  Json response;
  Aws::GameLift::Model::GameSession game_session;
  Json character_info = session->GetContext()["MyCharacter"];
  if (not instanceManager->CreateGameSession(character_info["Name"].GetString(), 1, game_session)) {
    response["msg"] = "Cannot create instance server. Sorry for lack of server resources. Please try later.";
  } else {
    string game_session_id = game_session.GetGameSessionId();
    Timer::ExpireAfter(WallClock::FromSec(3), [session, game_session_id, instanceManager](const Timer::Id &, const WallClock::Value &) {
      CheckAndMoveToInstance(session, game_session_id, instanceManager);
    });
    return;
  }
  session->SendMessage("error", response, kDefaultEncryption, kTcp);
}

void MoveToAnotherField(const Ptr<Session> &session, GameLiftClientManager *targetFieldManager, int field_index) {
  Json &character_info = session->GetContext()["MyCharacter"];
  string id = character_info["Name"].GetString();
  Json response;
  Aws::GameLift::Model::GameSession game_session;
  if (not targetFieldManager->SearchGameSessions(game_session)) {
    response["result"] = "nop";
    response["msg"] = "Cannot find created Game Session.";
  } else {
    Aws::GameLift::Model::PlayerSession player_session;
    if (not targetFieldManager->CreatePlayerSession(game_session.GetGameSessionId(), id, player_session)) {
      response["result"] = "nop";
      response["msg"] = "'CreatePlayerSession' failed.";
    } else {
      Rpc::PeerMap servers;
      size_t peer_count = Rpc::GetPeersWithTag(&servers, game_session.GetGameSessionId());
      if (peer_count == 0) {
        response["result"] = "nop";
        response["msg"] = "There is no available field server.";
      } else {
        character_info["Field"] = field_index;
        character_info["x"] = 0;
        character_info["y"] = 0;
        character_info["z"] = 0;
        character_info["ry"] = 0;
        character_info["v"] = 0;
        character_info["h"] = 0;
        BroadcastMessage(session->id(), "remove", session->GetContext()["MyCharacter"]);
        session->GetContext()["player_session_id"] = player_session.GetPlayerSessionId();
        if (AccountManager::RedirectClient(session, servers.begin()->first, session->GetContext().ToString())) {
          return;
        } else {
          response["result"] = "nop";
          response["msg"] = "Redirecting failed.";
        }
      }
    }
    session->SendMessage("error", response, kDefaultEncryption, kTcp);
  }
}

void OnPortal(const Ptr<Session> &session, const Json &message) {
  LOG(INFO) << "'portal' message recved.";
  int field_index = message["field_index"].GetInteger();
  GameLiftClientManager *targetFieldManager;
  if (field_index == 1) {
    targetFieldManager = GameLiftClientManagerMap->find("field")->second;
  } else {
    targetFieldManager = GameLiftClientManagerMap->find("field2")->second;
  }
  Json response;
  Aws::GameLift::Model::GameSession game_session;
  if (not targetFieldManager->SearchGameSessions(game_session)) {
    if (not targetFieldManager->CreateGameSession("field", 200, game_session)) {
      response["result"] = "nop";
      response["msg"] = "Cannot find available game session.";
      session->SendMessage("login", response, kDefaultEncryption, kTcp);
    } else {
      Timer::ExpireAfter(WallClock::FromSec(10), [session, targetFieldManager, field_index](const Timer::Id &, const WallClock::Value &) {
        MoveToAnotherField(session, targetFieldManager, field_index);
      });
      return;
    }
  } else {
    MoveToAnotherField(session, targetFieldManager, field_index);
  }
}

void OnClear(const Ptr<Session> &session, const Json &message) {
  LOG(INFO) << "'clear' message recved.";

  Json &character_info = session->GetContext()["MyCharacter"];
  Ptr<Character> character = Character::FetchByName(character_info["Name"].GetString());
  int level = character->GetLevel();
  level = level + 1;
  character_info["Level"].SetInteger(level);
  character->SetLevel(level);

  GameLiftClientManager *fieldManager = GameLiftClientManagerMap->find("field")->second;
  Json response;
  Aws::GameLift::Model::GameSession game_session;
  if (not fieldManager->SearchGameSessions(game_session)) {
    response["msg"] = "Cannot find created field server.";
  } else {
    Aws::GameLift::Model::PlayerSession player_session;
    Json character_info = session->GetContext()["MyCharacter"];
    if (not fieldManager->CreatePlayerSession(game_session.GetGameSessionId(), character_info["Name"].GetString(), player_session)) {
      response["msg"] = "'CreatePlayerSession' failed.";
    } else {
      Rpc::PeerMap servers;
      size_t peer_count = Rpc::GetPeersWithTag(&servers, game_session.GetGameSessionId());
      if (peer_count == 0) {
        response["msg"] = "There is no available field server.";
      } else {
        session->GetContext()["player_session_id"] = player_session.GetPlayerSessionId();
        if (AccountManager::RedirectClient(session, servers.begin()->first, session->GetContext().ToString())) {
          EndServerAfter5Sec();
          return;
        } else {
          response["msg"] = "Redirecting failed.";
        }
      }
    }
  }
  session->SendMessage("error", response, kDefaultEncryption, kTcp);
  EndServerAfter5Sec();
}

void CheckInstancePlayerConnection() {
  if (not GGameLiftSDKManager || not GGameLiftSDKManager->mGameSessionStarted) {
    Timer::ExpireAfter(WallClock::FromSec(3), [](const Timer::Id &, const WallClock::Value &) {
      CheckInstancePlayerConnection();
    });
  } else {
    Timer::ExpireAfter(WallClock::FromSec(30), [](const Timer::Id &, const WallClock::Value &) {
      if (not isPlayerComes) { EndServerAfter5Sec(); }
    });
  }
}

void RegisterEventHandlers() {
  HandlerRegistry::Install2(OnSessionOpened, OnSessionClosed);
  if (FLAGS_app_flavor == "field") {
    HandlerRegistry::RegisterTcpTransportDetachedHandler(OnTcpTransportDetached);
    HandlerRegistry::Register("relay", OnRelay);      // broadcasting character state
    HandlerRegistry::Register("dungeon", OnDungeon);  // moving to instance
    HandlerRegistry::Register("portal", OnPortal);    // moving to another field
  } else if (FLAGS_app_flavor == "instance") {
    HandlerRegistry::Register("clear", OnClear);      // instance cleared
    isPlayerComes = false;
    CheckInstancePlayerConnection();
  }
  AccountManager::RegisterRedirectionHandler(OnClientRedirected);
}

}  // namespace gamelift_demo
