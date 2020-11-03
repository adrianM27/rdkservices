/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include "FireboltMediaPlayer.h"
#include "utils.h"
#include "Module.h"

namespace WPEFramework {

    namespace Plugin {

        SERVICE_REGISTRATION(FireboltMediaPlayer, 1, 0);

        FireboltMediaPlayer::FireboltMediaPlayer()
        : _connectionId(0)
        , _notification(this)
        , _mediaPlayer(nullptr)
        , _mediaPlayerSink(this)
        , _id()
        , _idRefCnt(0)
        {
            RegisterAll();
        }

        FireboltMediaPlayer::~FireboltMediaPlayer()
        {
            UnregisterAll();
        }

        const string FireboltMediaPlayer::Initialize(PluginHost::IShell* service)
        {
            LOGINFO();
            string message;
            _connectionId = 0;
            _service = service;

            // Register the Process::Notification stuff. The Remote process might die before we get a
            // change to "register" the sink for these events !!! So do it ahead of instantiation.
            _service->Register(&_notification);

            _mediaPlayer = _service->Root<Exchange::IMediaPlayer>(_connectionId, 2000, "AampPlayerImplementation");

            if (_mediaPlayer) {
                LOGINFO("Successfully instantiated Firebolt Media Player");

                // Register events callback
                _mediaPlayer->RegisterCallback(&_mediaPlayerSink);

            } else {
                LOGERR("Firebolt Media Player could not be initialized.");
                message = "Firebolt Media Player could not be initialized.";
                _service->Unregister(&_notification);
            }

            return message;
        }

        void FireboltMediaPlayer::Deinitialize(PluginHost::IShell* service)
        {
            LOGINFO();
            ASSERT(_service == service);
            ASSERT(_mediaPlayer != nullptr);

            service->Unregister(&_notification);

            if (_mediaPlayer->Release() != Core::ERROR_DESTRUCTION_SUCCEEDED) {

                ASSERT(_connectionId != 0);

                LOGERR("OutOfProcess Plugin is not properly destructed. PID: %d", _connectionId);

                RPC::IRemoteConnection* connection(_service->RemoteConnection(_connectionId));

                // The connection can disappear in the meantime...
                if (connection != nullptr) {

                    // But if it did not dissapear in the meantime, forcefully terminate it. Shoot to kill :-)
                    connection->Terminate();
                    connection->Release();
                }
            }

            _mediaPlayer = nullptr;
            _service = nullptr;
        }

        string FireboltMediaPlayer::Information() const
        {
            LOGINFO();
            // No additional info to report.
            return (string());
        }

        void FireboltMediaPlayer::Deactivated(RPC::IRemoteConnection* connection)
        {
            LOGINFO();
            // This can potentially be called on a socket thread, so the deactivation (wich in turn kills this object) must be done
            // on a seperate thread. Also make sure this call-stack can be unwound before we are totally destructed.
            if (_connectionId == connection->Id()) {
                LOGINFO("Deactivating");
                ASSERT(_service != nullptr);
                Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }

        void FireboltMediaPlayer::RegisterAll()
        {
            Register(_T("create"), &FireboltMediaPlayer::create, this);
            Register(_T("release"), &FireboltMediaPlayer::release, this);
            Register(_T("load"), &FireboltMediaPlayer::load, this);
            Register(_T("play"), &FireboltMediaPlayer::play, this);
            Register(_T("pause"), &FireboltMediaPlayer::pause, this);
            Register(_T("seekTo"), &FireboltMediaPlayer::seekTo, this);
            Register(_T("stop"), &FireboltMediaPlayer::stop, this);
            Register(_T("initConfig"), &FireboltMediaPlayer::initConfig, this);
            Register(_T("initDRMConfig"), &FireboltMediaPlayer::initDRMConfig, this);
        }

        void FireboltMediaPlayer::UnregisterAll()
        {
            Unregister(_T("create"));
            Unregister(_T("release"));
            Unregister(_T("load"));
            Unregister(_T("play"));
            Unregister(_T("pause"));
            Unregister(_T("setPosition"));
            Unregister(_T("stop"));
            Unregister(_T("initConfig"));
            Unregister(_T("initDRMConfig"));
        }

        uint32_t FireboltMediaPlayer::create(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            returnIfStringParamNotFound(parameters, keyId);

            if(parameters[keyId].String().empty())
            {
                LOGERR("Argument \'%s\' is empty", keyId);
                returnResponse(false);
            }

            uint32_t result = Core::ERROR_NONE;
            string newId = parameters[keyId].String();
            if(newId == _id)
            {
                _idRefCnt++;
            }
            else if( (result = _mediaPlayer->Create(newId)) == Core::ERROR_NONE )
            {
                _id = newId;
                _idRefCnt = 1;
            }
            else
            {
                _id.clear();
                _idRefCnt = 0;
            }

            returnResponse(result == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::release(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            returnIfStringParamNotFound(parameters, keyId);
            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            uint32_t result = Core::ERROR_NONE;
            if(_idRefCnt > 1)
            {
                _idRefCnt--;
            }
            else if(_idRefCnt == 1)
            {
                if((result = _mediaPlayer->Destroy(_id)) == Core::ERROR_NONE)
                {
                    _idRefCnt = 0;
                    _id.clear();
                }
            }
            else
            {
                result == Core::ERROR_GENERAL;
            }

            returnResponse(result == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::load(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            const char *keyUrl = "url";
            const char *keyAutoPlay = "autoplay";
            returnIfStringParamNotFound(parameters, keyId);
            returnIfParamNotFound(parameters, keyUrl);

            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            string url = parameters[keyUrl].Value();

            bool autoPlay = true;
            if(parameters.HasLabel(keyAutoPlay))
            {
                autoPlay = parameters[keyUrl].Boolean();
            }

            returnResponse( _mediaPlayer->Load(_id, url, autoPlay) == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::play(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            returnIfStringParamNotFound(parameters, keyId);
            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            returnResponse(_mediaPlayer->Play(_id) == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::pause(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            returnIfStringParamNotFound(parameters, keyId);
            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            returnResponse(_mediaPlayer->Pause(_id) == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::seekTo(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            const char *keyPositionSec = "positionSec";
            returnIfStringParamNotFound(parameters, keyId);
            returnIfNumberParamNotFound(parameters, keyPositionSec);
            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            int positionSec = parameters[keyPositionSec].Number();
            returnResponse(_mediaPlayer->SeekTo(_id, positionSec) == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::stop(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            returnIfStringParamNotFound(parameters, keyId);
            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            returnResponse(_mediaPlayer->Stop(_id) == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::initConfig(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            const char *keyConfig = "config";
            returnIfStringParamNotFound(parameters, keyId);
            returnIfParamNotFound(parameters, keyConfig);
            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            string config = parameters[keyId].Value();
            returnResponse(_mediaPlayer->InitConfig(_id, config) == Core::ERROR_NONE);
        }

        uint32_t FireboltMediaPlayer::initDRMConfig(const JsonObject& parameters, JsonObject& response)
        {
            LOGINFOMETHOD();
            const char *keyId = "id";
            const char *keyConfig = "config";
            returnIfStringParamNotFound(parameters, keyId);
            returnIfParamNotFound(parameters, keyConfig);
            if(parameters[keyId].String() != _id)
            {
                LOGERR("Instace \'%s\' does not exist", parameters[keyId].String().c_str());
                returnResponse(false);
            }

            string config = parameters[keyId].Value();
            returnResponse(_mediaPlayer->InitDRMConfig(_id, config) == Core::ERROR_NONE);
        }

        void FireboltMediaPlayer::onMediaPlayerEvent(const string& id, const string &eventName, const string &parametersJson)
        {
            JsonObject parametersJsonObjWithId;
            parametersJsonObjWithId[id.c_str()] = JsonObject(parametersJson);

            // Notify to all with:
            // params : { "<id>" : { <parametersJson> } }
            sendNotify(eventName.c_str(), parametersJsonObjWithId);

            // Notify certain "id"s with:
            // params : { <parametersJson> }
            // TODO: Currently we cannot listen to this event from ThunderJS (?)
//            Notify(eventName.c_str(), parametersJsonObj, [&](const string& designator) -> bool {
//                const string designator_id = designator.substr(0, designator.find('.'));
//                return (idString == designator_id);
//            });
        }

    }
}
