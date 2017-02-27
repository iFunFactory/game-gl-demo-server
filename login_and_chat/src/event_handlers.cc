// PLEASE ADD YOUR EVENT HANDLER DECLARATIONS HERE.

#include "event_handlers.h"

#include <funapi.h>
#include <glog/logging.h>

#include "gamelift_demo_loggers.h"
#include "gamelift_demo_messages.pb.h"

#include "gamelift_client_manager.h"

DECLARE_string(app_flavor);

namespace gamelift_demo {

void OnSessionOpened(const Ptr<Session> &session) {
  logger::SessionOpened(to_string(session->id()), WallClock::Now());
}

void OnSessionClosed(const Ptr<Session> &session, SessionCloseReason reason) {
  logger::SessionClosed(to_string(session->id()), WallClock::Now());
}

void AsyncLoginCallback(const string &id, const Ptr<Session> &session, bool ret);

void AsyncLogoutCallback(const string &id, const Ptr<Session> &logout_session, bool ret, const Ptr<Session> &login_session) {
  if(ret) {
    if(logout_session)
      logout_session->Close();
    Json &context = login_session->GetContext();
    context["retry"] = true;
    AccountManager::CheckAndSetLoggedInAsync(id, login_session, AsyncLoginCallback);
  } else {
    Json response;
    response["msg"] = "Cannot login to server. Please retry later. (-2)";
    response["result"] = "nop";
    login_session->SendMessage("login", response, kDefaultEncryption, kTcp);
    login_session->Close();
  }
}

void CreatePlayerSessionAndResponse(const string &id, const Aws::GameLift::Model::GameSession &game_session, const Ptr<Session> &session) {
  Json response;
  Aws::GameLift::Model::PlayerSession player_session;
  if (Field1GameliftManager->CreatePlayerSession(game_session.GetGameSessionId(), id, player_session)) {
    Rpc::PeerMap servers;
    size_t peer_count = Rpc::GetPeersWithTag(&servers, game_session.GetGameSessionId());
    if(peer_count == 0) {
      response["result"] = "nop";
      response["msg"] = "There is no available field server.";
    } else {
      LOG(INFO) << "login successful: addr: " << game_session.GetIpAddress() << ":" << game_session.GetPort() << " pid : " << player_session.GetPlayerSessionId();
      session->GetContext()["player_session_id"] = player_session.GetPlayerSessionId();
      if (AccountManager::RedirectClient(session, servers.begin()->first, session->GetContext().ToString())) {
        return;
      } else {
        response["result"] = "nop";
        response["msg"] = "Redirecting failed.";
      }
    }
  } else {
    response["result"] = "nop";
    response["msg"] = "'CreatePlayerSession' failed.";
  }
  if (response["result"].GetString() != "ok") {
    LOG(INFO) << "login NOP: " << response["msg"].GetString();
  }
  session->SendMessage("login", response, kDefaultEncryption, kTcp);
}

void AsyncLoginCallback(const string &id, const Ptr<Session> &session, bool ret) {
  if(ret) {
    Ptr<User> user = User::FetchById(id);
    Ptr<Character> character = Character::FetchByName(id);

    Json response;
    if(not user) {
      LOG(INFO) << "New user comes. : " << id;
      user = User::Create(id);
      if(not character) {
        character = Character::Create(id);
      }
      character->SetLevel(1); character->SetField(1);
      character->Setx(0); character->Sety(0); character->Setz(0);
      character->Setry(0); character->Setv(0); character->Seth(0);
      user->SetMyCharacter(character);
    }

    Json fieldAnouncement;
    fieldAnouncement["field_index"] = character->GetField();
    session->SendMessage("field", fieldAnouncement, kDefaultEncryption, kTcp);
    
    Json user_data;
    user->ToJson(&user_data);
    session->SetContext(user_data);
    GameLiftManager* fieldServerManager = NULL;
    if (character->GetField() == 1) {
      fieldServerManager = Field1GameliftManager;
    } else if(character->GetField() == 2) {
      fieldServerManager = Field2GameliftManager;
    }
    if (fieldServerManager) {
      Aws::GameLift::Model::GameSession game_session;
      if (not fieldServerManager->SearchGameSessions(game_session)) {
        if(not fieldServerManager->CreateGameSession("field", 200, game_session)) {
          response["result"] = "nop";
          response["msg"] = "Cannot find available game session.";
        } else {
          Timer::ExpireAfter(WallClock::FromSec(10), [id, session, fieldServerManager](const Timer::Id &, const WallClock::Value &) {
            Aws::GameLift::Model::GameSession game_session;
            if(not fieldServerManager->SearchGameSessions(game_session)) {
              Json response;
              response["result"] = "nop";
              response["msg"] = "Cannot find created Game Session.";
              session->SendMessage("login", response, kDefaultEncryption, kTcp);
              return;
            }
            CreatePlayerSessionAndResponse(id, game_session, session);
          });
          return;
        }
      } else {
        CreatePlayerSessionAndResponse(id, game_session, session);
        return;
      }
    } else {
      response["result"] = "nop";
      response["msg"] = "Filed server index is not valid.";
    }
    if (response["result"].GetString() != "ok") {
      LOG(INFO) << "login NOP: " << response["msg"].GetString();
    }
    session->SendMessage("login", response, kDefaultEncryption, kTcp);
  } else {
    Json context = session->GetContext();
    if(context.HasAttribute("retry") && context["retry"].GetBool()) {
      Json response;
      response["msg"] = "Cannot login to server. Please retry later. (-1)";
      response["result"] = "nop";
      session->SendMessage("login", response, kDefaultEncryption, kTcp);
      session->Close();
    } else {
      AccountManager::SetLoggedOutGlobalAsync(id, 
          [session](const string &id, const Ptr<Session> &logout_session, bool ret)
          { AsyncLogoutCallback(id, logout_session, ret, session); });
    }
  }
}

void OnAccountLogin(const Ptr<Session> &session, const Json &message) {
  LOG(INFO) << "login " << message.ToString();
  string uid = message["uid"].GetString();
  AccountManager::CheckAndSetLoggedInAsync(uid, session, AsyncLoginCallback);
}

void RegisterEventHandlers() {
  HandlerRegistry::Install2(OnSessionOpened, OnSessionClosed);
  if (FLAGS_app_flavor == "login") {
    HandlerRegistry::Register("login", OnAccountLogin);
  }
}

}  // namespace gamelift_demo
