#include "main.hpp"
#include "GlobalNamespace/MainMenuViewController.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/SharedCoroutineStarter.hpp"
#include "GlobalNamespace/GamePause.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/PrimitiveType.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/Texture.hpp"
#include "UnityEngine/Video/VideoPlayer.hpp"
#include "UnityEngine/Video/VideoClip.hpp"
#include "UnityEngine/Video/VideoRenderMode.hpp"
#include "UnityEngine/Video/VideoPlayer_EventHandler.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/WaitForSeconds.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UI/VideoMenuViewController.hpp"
#include "questui/shared/QuestUI.hpp"
#include "questui/shared/CustomTypes/Components/MainThreadScheduler.hpp"
#include "questui/shared/ArrayUtil.hpp"
#include "VideoPlayer.hpp"
#include "custom-types/shared/coroutine.hpp"
#include "PythonInternal.hpp"
#include "Python.hpp"
#include "CustomLogger.hpp"
#include "Utils/FileUtils.hpp"
#include "assets.hpp"


using namespace UnityEngine;
using namespace GlobalNamespace;

static ModInfo modInfo; // Stores the ID and version of our mod, and is sent to the modloader upon startup

// Loads the config from disk using our modInfo, then returns it for use
Configuration& getConfig() {
    static Configuration config(modInfo);
    return config;
}

// Returns a logger, useful for printing debug messages
Logger& getLogger() {
    static Logger* logger = new Logger(modInfo);
    return *logger;
}

// Called at the early stages of game loading
extern "C" void setup(ModInfo& info) {
    info.id = ID;
    info.version = VERSION;
    modInfo = info;
	
    getConfig().Load(); // Load the config file
    getLogger().info("Completed setup!");
}

custom_types::Helpers::Coroutine coroutine(Cinema::VideoPlayer* videoPlayer, AudioSource* audioSource) {
    while(!audioSource->get_isPlaying()) co_yield nullptr;
    videoPlayer->set_time(-2040);
    videoPlayer->Play();
    co_return;
}

Cinema::VideoPlayer* videoPlayer = nullptr;

MAKE_HOOK_MATCH(GamePause_Resume, &GlobalNamespace::GamePause::Resume, void, GamePause* self) {
    GamePause_Resume(self);
    if(videoPlayer)
        videoPlayer->Play();
    getLogger().info("resume");
}

MAKE_HOOK_MATCH(GamePause_Pause, &GamePause::Pause, void, GamePause* self) {
    GamePause_Pause(self);
    if(videoPlayer) {
        videoPlayer->Pause();
        getLogger().info("pause");
    }

}

MAKE_HOOK_MATCH(SetupSongUI, &GlobalNamespace::AudioTimeSyncController::StartSong, void, GlobalNamespace::AudioTimeSyncController* self, float startTimeOffset) {
    SetupSongUI(self, startTimeOffset);

    GameObject* Mesh = GameObject::CreatePrimitive(PrimitiveType::Plane);
    auto material = QuestUI::ArrayUtil::Last(Resources::FindObjectsOfTypeAll<Material*>(), [](Material* x) {
        return x->get_name() == "PyroVideo (Instance)";
    });
    if(material)
        Mesh->GetComponent<Renderer*>()->set_material(material);
    else
        Mesh->GetComponent<Renderer*>()->set_material(Material::New_ctor(Shader::Find(il2cpp_utils::newcsstr("Unlit/Texture"))));
    Mesh->get_transform()->set_position(Vector3{0.0f, 12.4f, 67.8f});
    Mesh->get_transform()->set_rotation(Quaternion::Euler(90.0f, 270.0f, 90.0f));
    Mesh->get_transform()->set_localScale(Vector3(5.11, 1, 3));

    auto cinemaScreen = Mesh->GetComponent<Renderer*>();

    videoPlayer = Mesh->AddComponent<Cinema::VideoPlayer*>();
    videoPlayer->set_isLooping(true);
    videoPlayer->set_playOnAwake(false);
    videoPlayer->set_renderMode(Video::VideoRenderMode::MaterialOverride);
    videoPlayer->set_audioOutputMode(Video::VideoAudioOutputMode::None);
    videoPlayer->set_aspectRatio(Video::VideoAspectRatio::FitInside);
    if(cinemaScreen)
        videoPlayer->set_renderer(cinemaScreen);
    videoPlayer->set_url("/sdcard/EaswWiwMVs8.mp4");

    videoPlayer->Prepare();

    GlobalNamespace::SharedCoroutineStarter::get_instance()->StartCoroutine(custom_types::Helpers::CoroutineHelper::New(coroutine(videoPlayer, self->audioSource)));

    UnorderedEventCallback<int, char*> PythonWriteEvent;
    
    bool LoadPythonDirect() {
        auto pythonPath = FileUtils::getPythonPath();
        auto scriptsPath = FileUtils::getScriptsPath();
        auto pythonHome = pythonPath + "/usr";
        LOG_INFO("PythonPath: %s", pythonPath.c_str());
        if(!direxists(pythonHome)) {
            mkpath(pythonPath);
            FileUtils::ExtractZip(IncludedAssets::python_zip, pythonPath);
        }
        dlerror();
        auto libdl = dlopen("libdl.so", RTLD_NOW | RTLD_GLOBAL);
        auto libdlError = dlerror();
        if(libdlError) {
            LOG_ERROR("Couldn't dlopen libdl.so: %s", libdlError);
            return false;
        }
        LOAD_DLSYM(libdl, __loader_android_create_namespace);
        LOAD_DLSYM(libdl, __loader_android_dlopen_ext);

        auto ns = __loader_android_create_namespace(
            "python",
            ("/system/lib64/:/system/product/lib64/:" + pythonHome + "/lib").c_str(),
            "/system/lib64/",
            ANDROID_NAMESPACE_TYPE_SHARED |
            ANDROID_NAMESPACE_TYPE_ISOLATED,
            "/system/:/data/:/vendor/",
            NULL);
        if(!ns) {
            LOG_ERROR("Couldn't create namespace");
            return false;
        }
        const android_dlextinfo dlextinfo = {
                .flags = ANDROID_DLEXT_USE_NAMESPACE,
                .library_namespace = ns,
                };
        dlerror();
        auto libpython = __loader_android_dlopen_ext("libpython3.8.so", RTLD_LOCAL | RTLD_NOW, &dlextinfo);
        auto libpythonError = dlerror();
        if(libpythonError) {
            LOG_ERROR("Couldn't dlopen libpython3.8.so: %s", libpythonError);
            return false;
        }
        if(!Load_Dlsym(libpython)) {
            return false;
        }
        setenv("PYTHONHOME", pythonHome.c_str(), 1);     
        setenv("PYTHONPATH", scriptsPath.c_str(), 1);     
        setenv("SSL_CERT_FILE", (pythonHome + "/etc/tls/cert.pem").c_str(), 1); 
        return true;
    }

    bool LoadPython() {
        static std::optional<bool> loaded = std::nullopt;
        if(!loaded.has_value())
            loaded = LoadPythonDirect();
        return loaded.value();
    }

    void AddNativeModule(PyModuleDef& def) {
        PyObject* module = PyModule_Create2(&def, 3);
        PyObject* sys_modules = PyImport_GetModuleDict();
        PyDict_SetItemString(sys_modules, def.m_name, module);
        Py_DecRef(module);
    }

    bool Load_Dlsym(void* libpython) {
        dlerror();
        Py_None = reinterpret_cast<PyObject*>(dlsym(libpython, "_Py_NoneStruct"));
        auto Py_NoneError = dlerror(); 
        if(Py_NoneError) {
            LOG_ERROR("Couldn't dlsym %s: %s", "_Py_NoneStruct", Py_NoneError); 
            return false; 
}

#include "pythonlib/shared/Python.hpp"
#include "pythonlib/shared/Utils/FileUtils.hpp"
#include "pythonlib/shared/Utils/StringUtils.hpp"
#include "assets.hpp"

bool DownloadVideo(std::string_view url, std::function<void(float)> status = nullptr) {
    bool error = false;
    std::function<void(int, char*)> eventHandler = [status, &error](int type, char* data) {
        switch (type) {
        case 0:
            {
                std::string dataString(data);
                if(dataString.find("[download]", 0) != -1) {
                    auto pos = dataString.find("%", 0);
                    if(pos != -1 && pos > 5) {
                        auto percentange = dataString.substr(pos-5, 5);
                        if(percentange.find("]", 0) == 0) 
                            percentange = percentange.substr(1);
                        status(std::stof(percentange));
                    }
                }
            }
            break;
        case 1:
            error = true;
            getLogger().info("Error: %s", data);
            break;
        }
    };
    Python::PythonWriteEvent += eventHandler;
    std::string ytdlp = FileUtils::getScriptsPath() + "/yt_dlp";
    if(!direxists(ytdlp))
        FileUtils::ExtractZip(IncludedAssets::ytdlp_zip, ytdlp);
    Python::PyRun_SimpleString("from yt_dlp.__init__ import _real_main");
    std::string command = "_real_main([";
    for(auto splitted : StringUtils::Split("--no-cache-dir -o %(id)s.%(ext)s -P /sdcard " + url, " ")) {
        command += "\"" + splitted + "\",";
    }
    command = command.substr(0, command.length()-1) + "])";
    int result = Python::PyRun_SimpleString(command.c_str());
    Python::PythonWriteEvent -= eventHandler;
    return !error;
}

// Called later on in the game loading - a good time to install function hooks
extern "C" void load() {
    il2cpp_functions::Init();
    //INSTALL_HOOK(getLogger(), MainMenu);
    INSTALL_HOOK(getLogger(), SetupSongUI);
    INSTALL_HOOK(getLogger(), GamePause_Resume);
    INSTALL_HOOK(getLogger(), GamePause_Pause);

    QuestUI::Register::RegisterGameplaySetupMenu<Cinema::VideoMenuViewController*>(modInfo, "Cinema", QuestUI::Register::MenuType::Solo);

	custom_types::Register::AutoRegister();
    getLogger().info("DownloadVideo Result: %d", DownloadVideo("https://youtu.be/SnP0Nqp455I", [](float percentage) {
        getLogger().info("Download: %f", percentage);
    }));
    getLogger().info("DownloadVideo Result: %d", DownloadVideo("https://youtu.be/EaswWiwMVs8", [](float percentage) {
        getLogger().info("Download: %f", percentage);
    }));
}
