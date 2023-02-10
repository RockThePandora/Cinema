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
}

namespace Python {
        
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
	    PyObject* Py_None;

    DEFINE_DLSYM(PyObject *, PyMarshal_WriteObjectToString, PyObject *, int);
    DEFINE_DLSYM(void, PyThread_release_lock, PyThread_type_lock);
    DEFINE_DLSYM(void *, PyObject_Realloc, void *ptr, size_t new_size);
    DEFINE_DLSYM(PyObject *, PyNumber_Xor, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject *, PyBytes_DecodeEscape, const char *, Py_ssize_t,const char *, Py_ssize_t,const char *);
    DEFINE_DLSYM(PyObject *, PyImport_AddModule,const char *name            /* UTF-8 encoded string */);
    DEFINE_DLSYM(PyObject *, PyUnicodeTranslateError_GetObject, PyObject *);
    DEFINE_DLSYM(PyObject *, PyUnicodeDecodeError_GetEncoding, PyObject *);
    DEFINE_DLSYM(PyObject *, PyNumber_Negative, PyObject *o);
    DEFINE_DLSYM(Py_ssize_t, PyList_Size, PyObject *);
    DEFINE_DLSYM(int, PyModule_AddFunctions, PyObject *, PyMethodDef *);
    DEFINE_DLSYM(PyObject *, PyNumber_FloorDivide, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void, PyErr_Restore, PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM(void, PyObject_CallFinalizer, PyObject *);
    DEFINE_DLSYM(PyObject *, PySequence_Tuple, PyObject *o);
    DEFINE_DLSYM(int, PyCapsule_SetContext, PyObject *capsule, void *context);
    DEFINE_DLSYM(int, PySlice_Unpack, PyObject *slice,Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step);
    DEFINE_DLSYM(int, PyErr_BadArgument, void);
    DEFINE_DLSYM(PyObject *, PyObject_Format, PyObject *obj,PyObject *format_spec);
    DEFINE_DLSYM(int, PyUnicodeDecodeError_GetEnd, PyObject *, Py_ssize_t *);
    DEFINE_DLSYM_TYPE(PyLongRangeIter_Type);
    DEFINE_DLSYM(int, PyNumber_Check, PyObject *o);
    DEFINE_DLSYM(long, PyOS_strtol, const char *, char **, int);
    DEFINE_DLSYM(PyObject *, PyFile_OpenCodeObject, PyObject *path);
    DEFINE_DLSYM(int, PyUnicode_FSConverter, PyObject*, void*);
    DEFINE_DLSYM(void, PyMarshal_WriteObjectToFile, PyObject *, FILE *, int);
    DEFINE_DLSYM(void, PyPreConfig_InitIsolatedConfig, PyPreConfig *config);
    DEFINE_DLSYM(PyCapsule_Destructor, PyCapsule_GetDestructor, PyObject *capsule);
    DEFINE_DLSYM(PyObject *, PyImport_Import, PyObject *name);
    DEFINE_DLSYM(PyStatus, PyStatus_Ok, void);
    DEFINE_DLSYM(int, PyOS_mystrnicmp, const char *, const char *, Py_ssize_t);
    DEFINE_DLSYM(const Py_buffer *, PyPickleBuffer_GetBuffer, PyObject *);
    DEFINE_DLSYM(PyObject*, PyThread_GetInfo, void);
    DEFINE_DLSYM(int, PyFile_WriteString, const char *, PyObject *);
    DEFINE_DLSYM(size_t, PyThread_get_stacksize, void);
    DEFINE_DLSYM(int, PyToken_TwoChars, int, int);
    DEFINE_DLSYM(void, Py_SetPath, const wchar_t *);
    DEFINE_DLSYM_TYPE(PyMethodDescr_Type);
    DEFINE_DLSYM(Py_ssize_t, PySequence_Length, PyObject *o);
    DEFINE_DLSYM(PyObject *, PyType_GenericNew, PyTypeObject *,PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyTuple_GetItem, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(PyObject*, PyUnicode_Split,PyObject *s,                /* String to split */PyObject *sep,              /* String separator */Py_ssize_t maxsplit         /* Maxsplit count */);
    DEFINE_DLSYM(PyObject *, PyLong_FromSize_t, size_t);
    DEFINE_DLSYM(int, PyBuffer_IsContiguous, const Py_buffer *view, char fort);
    DEFINE_DLSYM(PyStatus, Py_InitializeFromConfig,const PyConfig *config);
    DEFINE_DLSYM(PyStatus, PyWideStringList_Append, PyWideStringList *list,const wchar_t *item);
    DEFINE_DLSYM(char *, PyOS_Readline, FILE *, FILE *, const char *);
    DEFINE_DLSYM(PyObject *, PyDict_GetItem, PyObject *mp, PyObject *key);
    DEFINE_DLSYM(PyObject*, PyUnicode_EncodeLocale,PyObject *unicode,const char *errors);
    DEFINE_DLSYM(PyObject *, PyErr_SetFromErrnoWithFilenameObjects,PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyUnicode_InternFromString,const char *u              /* UTF-8 encoded string */);
    DEFINE_DLSYM(void, PyErr_PrintEx, int);
    DEFINE_DLSYM(int, PyErr_ExceptionMatches, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_FromWideChar,const wchar_t *w,           /* wchar_t buffer */Py_ssize_t size             /* size of buffer */);
    DEFINE_DLSYM(PyObject *, PyCodec_LookupError, const char *name);
    DEFINE_DLSYM(PyObject *, PyComplex_FromCComplex, Py_complex);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_Tailmatch,PyObject *str,              /* String */PyObject *substr,           /* Prefix or Suffix string */Py_ssize_t start,           /* Start index */Py_ssize_t end,             /* Stop index */int direction               /* Tail end: -1 prefix, +1 suffix */);
    DEFINE_DLSYM(char *, PyBytes_AsString, PyObject *);
    DEFINE_DLSYM(void *, PyCapsule_Import,const char *name,           /* UTF-8 encoded string */int no_block);
    DEFINE_DLSYM_TYPE(PyMemoryView_Type);
    DEFINE_DLSYM(PyObject *, PyMarshal_ReadObjectFromString, const char *,Py_ssize_t);
    DEFINE_DLSYM(PyObject *, PyLong_FromUnicodeObject, PyObject *u, int base);
    DEFINE_DLSYM(PyStatus, PyConfig_SetArgv, PyConfig *config,Py_ssize_t argc,wchar_t * const *argv);
    DEFINE_DLSYM(PyStatus, Py_PreInitializeFromArgs,const PyPreConfig *src_config,Py_ssize_t argc,wchar_t **argv);
    DEFINE_DLSYM(int, PyToken_ThreeChars, int, int, int);
    DEFINE_DLSYM(int, PyUnicodeTranslateError_GetStart, PyObject *, Py_ssize_t *);
    DEFINE_DLSYM(int, PyUnicode_Contains,PyObject *container,        /* Container string */PyObject *element           /* Element string */);
    DEFINE_DLSYM(PyObject *, PyRun_StringFlags, const char *, int, PyObject *,PyObject *, PyCompilerFlags *);
    DEFINE_DLSYM(void, PySys_FormatStdout, const char *format, ...);
    DEFINE_DLSYM(int, PyImport_ImportFrozenModuleObject,PyObject *name);
    DEFINE_DLSYM(int, PyRun_AnyFileFlags, FILE *, const char *, PyCompilerFlags *);
    DEFINE_DLSYM(PyObject *, PyDictProxy_New, PyObject *);
    DEFINE_DLSYM_TYPE(PyDictIterKey_Type);
    DEFINE_DLSYM_TYPE(PyODictKeys_Type);
    DEFINE_DLSYM(PyObject *, PyFloat_FromString, PyObject*);
    DEFINE_DLSYM(int, PyList_Insert, PyObject *, Py_ssize_t, PyObject *);
    DEFINE_DLSYM(int, PyPickleBuffer_Release, PyObject *);
    DEFINE_DLSYM(const char *, PyEval_GetFuncDesc, PyObject *);
    DEFINE_DLSYM(int, PyTraceBack_Print, PyObject *, PyObject *);
    DEFINE_DLSYM(void, PySys_AddXOption, const wchar_t *);
    DEFINE_DLSYM(PyObject *, PyBytes_Repr, PyObject *, int);
    DEFINE_DLSYM(PyObject *, Py_BuildValue, const char *, ...);
    DEFINE_DLSYM(PyObject *, PyNumber_Lshift, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void, PyConfig_InitPythonConfig, PyConfig *config);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF8Stateful,const char *string,         /* UTF-8 encoded string */Py_ssize_t length,          /* size of string */const char *errors,         /* error handling */Py_ssize_t *consumed        /* bytes consumed */);
    DEFINE_DLSYM(int, PyBuffer_FromContiguous, Py_buffer *view, void *buf,Py_ssize_t len, char order);
    DEFINE_DLSYM(int, PyUnicodeDecodeError_SetReason,PyObject *exc,const char *reason          /* UTF-8 encoded string */);
    DEFINE_DLSYM(void, PyOS_AfterFork_Parent, void);
    DEFINE_DLSYM(int, PyDict_DelItemString, PyObject *dp, const char *key);
    DEFINE_DLSYM(PyObject *, PySequence_Repeat, PyObject *o, Py_ssize_t count);
    DEFINE_DLSYM(Py_ssize_t, PyTuple_Size, PyObject *);
    DEFINE_DLSYM(void, PyMem_SetAllocator, PyMemAllocatorDomain domain,PyMemAllocatorEx *allocator);
    DEFINE_DLSYM(PyObject*, PyUnicode_EncodeUTF7,PyObject *unicode,          /* Unicode object */int base64SetO,             /* Encode RFC2152 Set O characters in base64 */int base64WhiteSpace,       /* Encode whitespace (sp, ht, nl, cr) in base64 */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PyEval_EvalCode, PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceFloorDivide, PyObject *o1,PyObject *o2);
    DEFINE_DLSYM(int, PySet_Add, PyObject *set, PyObject *key);
    DEFINE_DLSYM(PyThread_type_lock, PyThread_allocate_lock, void);
    DEFINE_DLSYM(void, Py_ReprLeave, PyObject *);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_Fill,PyObject *unicode,Py_ssize_t start,Py_ssize_t length,Py_UCS4 fill_char);
    DEFINE_DLSYM(int, Py_AtExit, void (*func)(void));
    DEFINE_DLSYM(PyObject *, PyDict_New, void);
    DEFINE_DLSYM(int, PyCodec_RegisterError, const char *name, PyObject *error);
    DEFINE_DLSYM(int, PyIndex_Check, PyObject *);
    DEFINE_DLSYM(PyThreadState *, PyThreadState_New, PyInterpreterState *);
    DEFINE_DLSYM(int, PyUnicode_IsIdentifier, PyObject *s);
    DEFINE_DLSYM(PyObject *, PyUnicode_RichCompare,PyObject *left,             /* Left string */PyObject *right,            /* Right string */int op                      /* Operation: Py_EQ, Py_NE, Py_GT, etc. */);
    DEFINE_DLSYM(PyObject *, PyImport_ImportModuleLevelObject,PyObject *name,PyObject *globals,PyObject *locals,PyObject *fromlist,int level);
    DEFINE_DLSYM(PyObject *, PyObject_GetAttr, PyObject *, PyObject *);
    DEFINE_DLSYM_TYPE(PyUnicode_Type);
    DEFINE_DLSYM(PyStatus, PyStatus_Error, const char *err_msg);
    DEFINE_DLSYM(int, PyList_Sort, PyObject *);
    DEFINE_DLSYM(int, PyUnicodeDecodeError_SetEnd, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(PyObject *, PyModule_FromDefAndSpec2, PyModuleDef *def,PyObject *spec,int module_api_version);
    DEFINE_DLSYM(int, PyRun_InteractiveOneFlags,FILE *fp,const char *filename,       /* decoded from the filesystem encoding */PyCompilerFlags *flags);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceAnd, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject *, PyObject_CallMethod, PyObject *obj,const char *name,const char *format, ...);
    DEFINE_DLSYM(PyObject *, PyObject_Dir, PyObject *);
    DEFINE_DLSYM(void, PyThread_free_lock, PyThread_type_lock);
    DEFINE_DLSYM(int, PyObject_AsReadBuffer, PyObject *obj,const void **buffer,Py_ssize_t *buffer_len);
    DEFINE_DLSYM(const char *, PyCapsule_GetName, PyObject *capsule);
    DEFINE_DLSYM(PyObject *, PyCodec_StreamWriter,const char *encoding,PyObject *stream,const char *errors);
    DEFINE_DLSYM(int, PyArg_VaParse, PyObject *, const char *, va_list);
    DEFINE_DLSYM(PyObject *, PyBytes_FromStringAndSize, const char *, Py_ssize_t);
    DEFINE_DLSYM(PyObject *, PyLong_FromLong, long);
    DEFINE_DLSYM(PyObject *, PyList_New, Py_ssize_t size);
    DEFINE_DLSYM_TYPE(PyContextToken_Type);
    DEFINE_DLSYM(void, Py_Exit, int);
    DEFINE_DLSYM(void, PyUnicode_AppendAndDel,PyObject **pleft,           /* Pointer to left string */PyObject *right             /* Right string */);
    DEFINE_DLSYM(int, PySequence_In, PyObject *o, PyObject *value);
    DEFINE_DLSYM(PyObject *, PyUnicodeEncodeError_GetEncoding, PyObject *);
    DEFINE_DLSYM_TYPE(PyCoro_Type);
    DEFINE_DLSYM(int, PyList_SetItem, PyObject *, Py_ssize_t, PyObject *);
    DEFINE_DLSYM(PyStatus, PyStatus_Exit, int exitcode);
    DEFINE_DLSYM_TYPE(PyCFunction_Type);
    DEFINE_DLSYM(int, PyObject_GenericSetAttr, PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM(int, PySys_Audit,const char *event,const char *argFormat,...);
    DEFINE_DLSYM(PyHash_FuncDef*, PyHash_GetFuncDef, void);
    DEFINE_DLSYM(Py_ssize_t, PyMapping_Length, PyObject *o);
    DEFINE_DLSYM_TYPE(PyODictIter_Type);
    DEFINE_DLSYM(PyObject *, PyObject_Init, PyObject *, PyTypeObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeLatin1,const char *string,         /* Latin-1 encoded string */Py_ssize_t length,          /* size of string */const char *errors          /* error handling */);
    DEFINE_DLSYM(int, PyArg_VaParseTupleAndKeywords, PyObject *, PyObject *,const char *, char **, va_list);
    DEFINE_DLSYM(PyThreadState *, PyInterpreterState_ThreadHead, PyInterpreterState *);
    DEFINE_DLSYM_TYPE(PyFloat_Type);
    DEFINE_DLSYM(PyObject *, PyWrapper_New, PyObject *, PyObject *);
    DEFINE_DLSYM_TYPE(PyClassMethod_Type);
    DEFINE_DLSYM(void, PyThreadState_Clear, PyThreadState *);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_Find,PyObject *str,              /* String */PyObject *substr,           /* Substring to find */Py_ssize_t start,           /* Start index */Py_ssize_t end,             /* Stop index */int direction               /* Find direction: +1 forward, -1 backward */);
    DEFINE_DLSYM(int, PyStructSequence_InitType2, PyTypeObject *type,PyStructSequence_Desc *desc);
    DEFINE_DLSYM(void, PyErr_GetExcInfo, PyObject **, PyObject **, PyObject **);
    DEFINE_DLSYM(PyObject *, PyNumber_ToBase, PyObject *n, int base);
    DEFINE_DLSYM(PyObject *, PyNumber_Subtract, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void, PySys_SetArgv, int, wchar_t **);
    DEFINE_DLSYM(PyObject *, PyType_GenericAlloc, PyTypeObject *, Py_ssize_t);
    DEFINE_DLSYM_TYPE(PyFrozenSet_Type);
    DEFINE_DLSYM_TYPE(PyListRevIter_Type);
    DEFINE_DLSYM_TYPE(PySetIter_Type);
    DEFINE_DLSYM(PyObject *, PyImport_ExecCodeModuleEx,const char *name,           /* UTF-8 encoded string */PyObject *co,const char *pathname        /* decoded from the filesystem encoding */);
    DEFINE_DLSYM(int, PyErr_WarnExplicitObject,PyObject *category,PyObject *message,PyObject *filename,int lineno,PyObject *module,PyObject *registry);
    DEFINE_DLSYM_TYPE(PyStringIO_Type);
    DEFINE_DLSYM(int, PySequence_DelSlice, PyObject *o, Py_ssize_t i1, Py_ssize_t i2);
    DEFINE_DLSYM(int, PySequence_Contains, PyObject *seq, PyObject *ob);
    DEFINE_DLSYM(int, PyDict_Update, PyObject *mp, PyObject *other);
    DEFINE_DLSYM(const char *, Py_GetCopyright, void);
    DEFINE_DLSYM_TYPE(PySuper_Type);
    DEFINE_DLSYM_TYPE(PyModuleDef_Type);
    DEFINE_DLSYM(void, PySys_ResetWarnOptions, void);
    DEFINE_DLSYM(const char *, PyUnicode_AsUTF8AndSize,PyObject *unicode,Py_ssize_t *size);
    DEFINE_DLSYM(int, PyObject_RichCompareBool, PyObject *, PyObject *, int);
    DEFINE_DLSYM(PyObject *, PyObject_Bytes, PyObject *);
    DEFINE_DLSYM(PyObject *, PyInstanceMethod_New, PyObject *);
    DEFINE_DLSYM(int, PyContext_Enter, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsRawUnicodeEscapeString,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM(PyStatus, PyConfig_SetBytesString,PyConfig *config,wchar_t **config_str,const char *str);
    DEFINE_DLSYM_TYPE(PyModule_Type);
    DEFINE_DLSYM(PyCodeObject *, PyCode_NewWithPosOnlyArgs,int, int, int, int, int, int, PyObject *, PyObject *,PyObject *, PyObject *, PyObject *, PyObject *,PyObject *, PyObject *, int, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_EncodeCharmap,PyObject *unicode,          /* Unicode object */PyObject *mapping,          /* encoding mapping */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PyTuple_New, Py_ssize_t size);
    DEFINE_DLSYM(int, PyFile_WriteObject, PyObject *, PyObject *, int);
    DEFINE_DLSYM(int, PyCapsule_SetDestructor, PyObject *capsule, PyCapsule_Destructor destructor);
    DEFINE_DLSYM(int, PyStatus_Exception, PyStatus err);
    DEFINE_DLSYM(int, PyThread_set_stacksize, size_t);
    DEFINE_DLSYM_TYPE(PyBufferedIOBase_Type);
    DEFINE_DLSYM(double, PyLong_AsDouble, PyObject *);
    DEFINE_DLSYM(PyObject *, PyFunction_GetGlobals, PyObject *);
    DEFINE_DLSYM(PyObject *, PyLong_GetInfo, void);
    DEFINE_DLSYM(int, PyMarshal_ReadShortFromFile, FILE *);
    DEFINE_DLSYM(int, PySlice_GetIndices, PyObject *r, Py_ssize_t length,Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step);
    DEFINE_DLSYM(PyObject *, PyModule_GetNameObject, PyObject *);
    DEFINE_DLSYM(PyObject *, PyRun_FileEx, FILE *fp, const char *p, int s, PyObject *g, PyObject *l, int c);
    DEFINE_DLSYM_TYPE(PyClassMethodDescr_Type);
    DEFINE_DLSYM_TYPE(PyFunction_Type);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsLatin1String,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM(void, PyInterpreterState_Delete, PyInterpreterState *);
    DEFINE_DLSYM(void, PyEval_RestoreThread, PyThreadState *);
    DEFINE_DLSYM(PyObject *, PyODict_New, void);
    DEFINE_DLSYM(int, PyUnicode_CompareWithASCIIString,PyObject *left,const char *right           /* ASCII-encoded string */);
    DEFINE_DLSYM(void, Py_SetPythonHome, const wchar_t *);
    DEFINE_DLSYM(const char *, PyEval_GetFuncName, PyObject *);
    DEFINE_DLSYM(Py_complex, PyComplex_AsCComplex, PyObject *op);
    DEFINE_DLSYM(wchar_t *, Py_GetProgramName, void);
    DEFINE_DLSYM(int, PyContextVar_Get,PyObject *var, PyObject *default_value, PyObject **value);
    DEFINE_DLSYM(PyObject *, PyNumber_Multiply, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void *, PyObject_Calloc, size_t nelem, size_t elsize);
    DEFINE_DLSYM(void *, PyMem_Calloc, size_t nelem, size_t elsize);
    DEFINE_DLSYM(PyObject *, PyCFunction_GetSelf, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_Splitlines,PyObject *s,                /* String to split */int keepends                /* If true, line end markers are included */);
    DEFINE_DLSYM(int, PySequence_DelItem, PyObject *o, Py_ssize_t i);
    DEFINE_DLSYM_TYPE(PyGen_Type);
    DEFINE_DLSYM(void, PyConfig_Clear, PyConfig *);
    DEFINE_DLSYM(void *, PyCapsule_GetContext, PyObject *capsule);
    DEFINE_DLSYM(PyObject *, PyCodec_StrictErrors, PyObject *exc);
    DEFINE_DLSYM_TYPE(PyLong_Type);
    DEFINE_DLSYM(PyObject *, PySys_GetXOptions, void);
    DEFINE_DLSYM(Py_UCS4, PyUnicode_ReadChar,PyObject *unicode,Py_ssize_t index);
    DEFINE_DLSYM(int, PyErr_WarnFormat,PyObject *category,Py_ssize_t stack_level,const char *format,         /* ASCII-encoded string  */...);
    DEFINE_DLSYM(int, Py_FrozenMain, int argc, char **argv);
    DEFINE_DLSYM(PyObject *, PyDict_Items, PyObject *mp);
    DEFINE_DLSYM(PyObject *, PyUnicodeTranslateError_Create,PyObject *object,Py_ssize_t start,Py_ssize_t end,const char *reason          /* UTF-8 encoded string */);
    DEFINE_DLSYM(int, PyList_SetSlice, PyObject *, Py_ssize_t, Py_ssize_t, PyObject *);
    DEFINE_DLSYM(PyInterpreterState *, PyInterpreterState_New, void);
    DEFINE_DLSYM(int, PyCodec_Register,PyObject *search_function);
    DEFINE_DLSYM(int, PyObject_AsWriteBuffer, PyObject *obj,void **buffer,Py_ssize_t *buffer_len);
    DEFINE_DLSYM(PyObject *, PyWeakref_NewRef, PyObject *ob,PyObject *callback);
    DEFINE_DLSYM(PyObject*, PyUnicode_EncodeFSDefault,PyObject *unicode);
    DEFINE_DLSYM(PyObject *, PyContext_CopyCurrent, void);
    DEFINE_DLSYM(const char *, PyExceptionClass_Name, PyObject *);
    DEFINE_DLSYM(PyObject *, PySys_GetObject, const char *);
    DEFINE_DLSYM(int, PyUnicodeEncodeError_SetReason,PyObject *exc,const char *reason          /* UTF-8 encoded string */);
    DEFINE_DLSYM(int, PyUnicode_Resize,PyObject **unicode,         /* Pointer to the Unicode object */Py_ssize_t length           /* New length */);
    DEFINE_DLSYM(Py_ssize_t, PySequence_Index, PyObject *o, PyObject *value);
    DEFINE_DLSYM(unsigned long, PyType_GetFlags, PyTypeObject*);
    DEFINE_DLSYM(PyObject *, PyDict_Keys, PyObject *mp);
    DEFINE_DLSYM_TYPE(PyCell_Type);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeLocaleAndSize,const char *str,Py_ssize_t len,const char *errors);
    DEFINE_DLSYM(int, PyErr_WarnExplicit,PyObject *category,const char *message,        /* UTF-8 encoded string */const char *filename,       /* decoded from the filesystem encoding */int lineno,const char *module,         /* UTF-8 encoded string */PyObject *registry);
    DEFINE_DLSYM(long, PyImport_GetMagicNumber, void);
    DEFINE_DLSYM(int, PyCapsule_IsValid, PyObject *capsule, const char *name);
    DEFINE_DLSYM(PyObject *, PyFile_FromFd, int, const char *, const char *, int,const char *, const char *,const char *, int);
    DEFINE_DLSYM_TYPE(PyInstanceMethod_Type);
    DEFINE_DLSYM_TYPE(PyZip_Type);
    DEFINE_DLSYM(void, Py_Finalize, void);
    DEFINE_DLSYM(double, PyFloat_GetMax, void);
    DEFINE_DLSYM(PyGILState_STATE, PyGILState_Ensure, void);
    DEFINE_DLSYM(PyObject *, PySequence_List, PyObject *o);
    DEFINE_DLSYM(PyObject *, PyCodec_IncrementalDecoder,const char *encoding,const char *errors);
    DEFINE_DLSYM(void, PyErr_Clear, void);
    DEFINE_DLSYM(Py_ssize_t, PyObject_Length, PyObject *o);
    DEFINE_DLSYM(PyObject *, PyCapsule_New,void *pointer,const char *name,PyCapsule_Destructor destructor);
    DEFINE_DLSYM(PyObject *, PyLong_FromSsize_t, Py_ssize_t);
    DEFINE_DLSYM_TYPE(PyByteArray_Type);
    DEFINE_DLSYM(void, PyThreadState_Delete, PyThreadState *);
    DEFINE_DLSYM(PyObject *, PyObject_GenericGetDict, PyObject *, void *);
    DEFINE_DLSYM(PyObject *, PyContext_New, void);
    DEFINE_DLSYM(PyStatus, PyConfig_Read, PyConfig *config);
    DEFINE_DLSYM(void, PyThreadState_DeleteCurrent, void);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeASCII,const char *string,         /* ASCII encoded string */Py_ssize_t length,          /* size of string */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PyUnicodeTranslateError_GetReason, PyObject *);
    DEFINE_DLSYM_TYPE(PyTraceBack_Type);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceMultiply, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(int, PyArg_ValidateKeywordArguments, PyObject *);
    DEFINE_DLSYM(void*, PyModule_GetState, PyObject*);
    DEFINE_DLSYM_TYPE(PyWrapperDescr_Type);
    DEFINE_DLSYM(PyObject *, PyLong_FromVoidPtr, void *);
    DEFINE_DLSYM(int, PyModule_SetDocString, PyObject *, const char *);
    DEFINE_DLSYM(PyObject *, PyErr_NewExceptionWithDoc,const char *name, const char *doc, PyObject *base, PyObject *dict);
    DEFINE_DLSYM(void, PyErr_SyntaxLocationEx,const char *filename,       /* decoded from the filesystem encoding */int lineno,int col_offset);
    DEFINE_DLSYM(void *, PyObject_Malloc, size_t size);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceRemainder, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void *, PyThread_tss_get, Py_tss_t *key);
    DEFINE_DLSYM(void, PyOS_AfterFork_Child, void);
    DEFINE_DLSYM_TYPE(PyDictIterItem_Type);
    DEFINE_DLSYM(PyObject *, PySet_Pop, PyObject *set);
    DEFINE_DLSYM(int, PyObject_DelItem, PyObject *o, PyObject *key);
    DEFINE_DLSYM(PyObject *, PySeqIter_New, PyObject *);
    DEFINE_DLSYM_TYPE(PyBufferedReader_Type);
    DEFINE_DLSYM(int, PySequence_SetSlice, PyObject *o, Py_ssize_t i1, Py_ssize_t i2,PyObject *v);
    DEFINE_DLSYM(char *, PyOS_double_to_string, double val,char format_code,int precision,int flags,int *type);
    DEFINE_DLSYM(int, PyObject_IsTrue, PyObject *);
    DEFINE_DLSYM(PyObject *, PyImport_GetModule, PyObject *name);
    DEFINE_DLSYM(int, PyStatus_IsError, PyStatus err);
    DEFINE_DLSYM(PyObject *, PyFloat_GetInfo, void);
    DEFINE_DLSYM(int, PyRun_SimpleString, const char *s);
    DEFINE_DLSYM(int, PyCapsule_SetPointer, PyObject *capsule, void *pointer);
    DEFINE_DLSYM(void, PyObject_GetArenaAllocator, PyObjectArenaAllocator *allocator);
    DEFINE_DLSYM(PyObject *, PyUnicode_FromFormat,const char *format,   /* ASCII-encoded string  */...);
    DEFINE_DLSYM(unsigned long long, PyLong_AsUnsignedLongLong, PyObject *);
    DEFINE_DLSYM(PyObject *, PyObject_Str, PyObject *);
    DEFINE_DLSYM(PyObject *, PyObject_CallMethodObjArgs,PyObject *obj,PyObject *name,...);
    DEFINE_DLSYM(PyObject *, PyException_GetCause, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsUTF32String,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM(void, Py_IncRef, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF16Stateful,const char *string,         /* UTF-16 encoded string */Py_ssize_t length,          /* size of string */const char *errors,         /* error handling */int *byteorder,             /* pointer to byteorder to use0=native;-1=LE,1=BE; updated onexit */Py_ssize_t *consumed        /* bytes consumed */);
    DEFINE_DLSYM(int, PySequence_SetItem, PyObject *o, Py_ssize_t i, PyObject *v);
    DEFINE_DLSYM(PyObject *, PyTuple_Pack, Py_ssize_t, ...);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_CopyCharacters,PyObject *to,Py_ssize_t to_start,PyObject *from,Py_ssize_t from_start,Py_ssize_t how_many);
    DEFINE_DLSYM_TYPE(PyStaticMethod_Type);
    DEFINE_DLSYM(Py_UCS4*, PyUnicode_AsUCS4,PyObject *unicode,Py_UCS4* buffer,Py_ssize_t buflen,int copy_null);
    DEFINE_DLSYM(PyObject *, PyCell_New, PyObject *);
    DEFINE_DLSYM_TYPE(PyEllipsis_Type);
    DEFINE_DLSYM(Py_ssize_t, PyDict_Size, PyObject *mp);
    DEFINE_DLSYM(void, Py_SetRecursionLimit, int);
    DEFINE_DLSYM(PyObject *, PyNumber_Add, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(double, PyComplex_ImagAsDouble, PyObject *op);
    DEFINE_DLSYM(wchar_t *, Py_GetExecPrefix, void);
    DEFINE_DLSYM(int, PyUnicodeDecodeError_GetStart, PyObject *, Py_ssize_t *);
    DEFINE_DLSYM(PyObject *, PyMemoryView_FromBuffer, Py_buffer *info);
    DEFINE_DLSYM(PyObject *, PyComplex_FromDoubles, double real, double imag);
    DEFINE_DLSYM(int, PyTraceMalloc_Untrack,unsigned int domain,uintptr_t ptr);
    DEFINE_DLSYM(PyObject *, PyDescr_NewMethod, PyTypeObject *, PyMethodDef *);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsASCIIString,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM(int, PyThreadState_SetAsyncExc, unsigned long, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_Concat,PyObject *left,             /* Left string */PyObject *right             /* Right string */);
    DEFINE_DLSYM(PyObject *, Py_CompileStringExFlags,const char *str,const char *filename,       /* decoded from the filesystem encoding */int start,PyCompilerFlags *flags,int optimize);
    DEFINE_DLSYM(PyObject *, PyGen_New, PyFrameObject *);
    DEFINE_DLSYM(PyObject *, PyRun_FileExFlags,FILE *fp,const char *filename,       /* decoded from the filesystem encoding */int start,PyObject *globals,PyObject *locals,int closeit,PyCompilerFlags *flags);
    DEFINE_DLSYM(PyObject *, PyCodec_BackslashReplaceErrors, PyObject *exc);
    DEFINE_DLSYM(int, PyArg_ParseTuple, PyObject *, const char *, ...);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF7Stateful,const char *string,         /* UTF-7 encoded string */Py_ssize_t length,          /* size of string */const char *errors,         /* error handling */Py_ssize_t *consumed        /* bytes consumed */);
    DEFINE_DLSYM(PyObject *, PyObject_Call, PyObject *callable,PyObject *args, PyObject *kwargs);
    DEFINE_DLSYM_TYPE(PyMap_Type);
    DEFINE_DLSYM(int, PyRun_AnyFile, FILE *fp, const char *name);
    DEFINE_DLSYM(PyObject *, PyObject_CallFunction, PyObject *callable,const char *format, ...);
    DEFINE_DLSYM(void, Py_FatalError, const char *message);
    DEFINE_DLSYM(void *, PyMem_Realloc, void *ptr, size_t new_size);
    DEFINE_DLSYM(PyObject *, PyFunction_GetKwDefaults, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_New,Py_ssize_t size,            /* Number of code points in the new string */Py_UCS4 maxchar             /* maximum code point value in the string */);
    DEFINE_DLSYM_TYPE(PyListIter_Type);
    DEFINE_DLSYM(PyObject *, PyUnicode_Format,PyObject *format,           /* Format string */PyObject *args              /* Argument tuple or dictionary */);
    DEFINE_DLSYM(Py_ssize_t, PySet_Size, PyObject *anyset);
    DEFINE_DLSYM(int, PyObject_SetAttr, PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM(int, PyUnicodeEncodeError_SetStart, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(PyObject *, PyCodec_NameReplaceErrors, PyObject *exc);
    DEFINE_DLSYM(Py_tss_t *, PyThread_tss_alloc, void);
    DEFINE_DLSYM(Py_hash_t, PyObject_HashNotImplemented, PyObject *);
    DEFINE_DLSYM(int, PyObject_IsSubclass, PyObject *object, PyObject *typeorclass);
    DEFINE_DLSYM(PyObject *, PyErr_SetFromErrno, PyObject *);
    DEFINE_DLSYM(PyObject *, PyFunction_GetCode, PyObject *);
    DEFINE_DLSYM(PyThreadState *, Py_NewInterpreter, void);
    DEFINE_DLSYM(PyObject *, PyModule_New,const char *name            /* UTF-8 encoded string */);
    DEFINE_DLSYM(void, PyBuffer_FillContiguousStrides, int ndims,Py_ssize_t *shape,Py_ssize_t *strides,int itemsize,char fort);
    DEFINE_DLSYM(PyObject *, PyObject_SelfIter, PyObject *);
    DEFINE_DLSYM(PyObject *, PyMemoryView_GetContiguous, PyObject *base,int buffertype,char order);
    DEFINE_DLSYM(const char *, PyModule_GetName, PyObject *);
    DEFINE_DLSYM(PyObject *, PyPickleBuffer_FromObject, PyObject *);
    DEFINE_DLSYM(wchar_t *, Py_GetPythonHome, void);
    DEFINE_DLSYM(int, PyCapsule_SetName, PyObject *capsule, const char *name);
    DEFINE_DLSYM_TYPE(PyReversed_Type);
    DEFINE_DLSYM(PyCFunction, PyCFunction_GetFunction, PyObject *);
    DEFINE_DLSYM(int, PyCFunction_GetFlags, PyObject *);
    DEFINE_DLSYM_TYPE(PyPickleBuffer_Type);
    DEFINE_DLSYM(void, PyEval_SetProfile, Py_tracefunc, PyObject *);
    DEFINE_DLSYM(int, PyObject_HasAttr, PyObject *, PyObject *);
    DEFINE_DLSYM(int, PySys_AddAuditHook, Py_AuditHookFunction, void*);
    DEFINE_DLSYM(PyObject *, PyObject_Type, PyObject *o);
    DEFINE_DLSYM(PyObject *, PyImport_ExecCodeModuleWithPathnames,const char *name,           /* UTF-8 encoded string */PyObject *co,const char *pathname,       /* decoded from the filesystem encoding */const char *cpathname       /* decoded from the filesystem encoding */);
    DEFINE_DLSYM(PyObject *, PySequence_GetItem, PyObject *o, Py_ssize_t i);
    DEFINE_DLSYM(PyObject *, PyMember_GetOne, const char *, struct PyMemberDef *);
    DEFINE_DLSYM(void, PySys_FormatStderr, const char *format, ...);
    DEFINE_DLSYM(void, PyStructSequence_InitType, PyTypeObject *type,PyStructSequence_Desc *desc);
    DEFINE_DLSYM(int, PyUnicode_FSDecoder, PyObject*, void*);
    DEFINE_DLSYM(int, PyType_Ready, PyTypeObject *);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlacePower, PyObject *o1, PyObject *o2,PyObject *o3);
    DEFINE_DLSYM(unsigned int, PyType_ClearCache, void);
    DEFINE_DLSYM(const char *, Py_GetPlatform, void);
    DEFINE_DLSYM(int, PyRun_InteractiveLoop, FILE *f, const char *p);
    DEFINE_DLSYM(PyObject *, PyDict_Values, PyObject *mp);
    DEFINE_DLSYM(PyObject *, PyNumber_Long, PyObject *o);
    DEFINE_DLSYM(void, PyErr_SetString,PyObject *exception,const char *string   /* decoded from utf-8 */);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceTrueDivide, PyObject *o1,PyObject *o2);
    DEFINE_DLSYM(PyObject *, PyInterpreterState_GetDict, PyInterpreterState *);
    DEFINE_DLSYM(void, PyDict_Clear, PyObject *mp);
    DEFINE_DLSYM(int, PyState_RemoveModule, struct PyModuleDef*);
    DEFINE_DLSYM(void, PyException_SetContext, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceLshift, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject *, PyBool_FromLong, long);
    DEFINE_DLSYM(PyObject *, PyMemoryView_FromObject, PyObject *base);
    DEFINE_DLSYM(PyObject*, PyStructSequence_GetItem, PyObject*, Py_ssize_t);
    DEFINE_DLSYM(PyObject*, PyUnicode_Partition,PyObject *s,                /* String to partition */PyObject *sep               /* String separator */);
    DEFINE_DLSYM(PyObject*, PyCode_Optimize, PyObject *code, PyObject* consts,PyObject *names, PyObject *lnotab);
    DEFINE_DLSYM(PyTryBlock *, PyFrame_BlockPop, PyFrameObject *);
    DEFINE_DLSYM(void, PyThread_tss_delete, Py_tss_t *key);
    DEFINE_DLSYM(PyObject *, PyMapping_Values, PyObject *o);
    DEFINE_DLSYM(int, PyBuffer_ToContiguous, void *buf, Py_buffer *view,Py_ssize_t len, char order);
    DEFINE_DLSYM(PyInterpreterState *, PyInterpreterState_Next, PyInterpreterState *);
    DEFINE_DLSYM(PyObject *, PyErr_Occurred, void);
    DEFINE_DLSYM(int, Py_AddPendingCall, int (*func)(void *), void *arg);
    DEFINE_DLSYM(PyObject *, PyFrozenSet_New, PyObject *);
    DEFINE_DLSYM(int, PyModule_AddObject, PyObject *mod, const char *, PyObject *value);
    DEFINE_DLSYM(int, PyThread_tss_is_created, Py_tss_t *key);
    DEFINE_DLSYM(wchar_t *, Py_DecodeLocale,const char *arg,size_t *size);
    DEFINE_DLSYM(int, PyErr_GivenExceptionMatches, PyObject *, PyObject *);
    DEFINE_DLSYM(unsigned long, PyThread_start_new_thread, void (*)(void *), void *);
    DEFINE_DLSYM(int, PyRun_InteractiveLoopFlags,FILE *fp,const char *filename,       /* decoded from the filesystem encoding */PyCompilerFlags *flags);
    DEFINE_DLSYM(void, PyInterpreterState_Clear, PyInterpreterState *);
    DEFINE_DLSYM_TYPE(PyBytesIO_Type);
    DEFINE_DLSYM(PyObject*, PyUnicode_BuildEncodingMap,PyObject* string            /* 256 character map */);
    DEFINE_DLSYM(wchar_t *, Py_GetPrefix, void);
    DEFINE_DLSYM_TYPE(PyList_Type);
    DEFINE_DLSYM(int, PyObject_CallFinalizerFromDealloc, PyObject *);
    DEFINE_DLSYM(PyObject *, PyMarshal_ReadLastObjectFromFile, FILE *);
    DEFINE_DLSYM(int, PyErr_ResourceWarning,PyObject *source,Py_ssize_t stack_level,const char *format,         /* ASCII-encoded string  */...);
    DEFINE_DLSYM(PyObject *, PyCodec_Decoder,const char *encoding);
    DEFINE_DLSYM(void, PySys_SetPath, const wchar_t *);
    DEFINE_DLSYM(PyObject *, PyNumber_Rshift, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsCharmapString,PyObject *unicode,          /* Unicode object */PyObject *mapping           /* encoding mapping */);
    DEFINE_DLSYM(PyOS_sighandler_t, PyOS_getsig, int);
    DEFINE_DLSYM(PyObject *, PyByteArray_FromStringAndSize, const char *, Py_ssize_t);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceXor, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void *, PyBuffer_GetPointer, Py_buffer *view, Py_ssize_t *indices);
    DEFINE_DLSYM(PyObject *, PyByteArray_FromObject, PyObject *);
    DEFINE_DLSYM(PyObject *, PyLong_FromDouble, double);
    DEFINE_DLSYM(void, Py_InitializeEx, int);
    DEFINE_DLSYM(PyObject *, PyNumber_Positive, PyObject *o);
    DEFINE_DLSYM(Py_ssize_t, PyBytes_Size, PyObject *);
    DEFINE_DLSYM_TYPE(PyDictProxy_Type);
    DEFINE_DLSYM_TYPE(PyBufferedRandom_Type);
    DEFINE_DLSYM(char *, Py_UniversalNewlineFgets, char *, int, FILE*, PyObject *);
    DEFINE_DLSYM(void, PyErr_Print, void);
    DEFINE_DLSYM(PyObject *, PySequence_InPlaceConcat, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(int, PyStatus_IsExit, PyStatus err);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceRshift, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(Py_ssize_t, PySequence_Size, PyObject *o);
    DEFINE_DLSYM(PyObject *, PyObject_GetItem, PyObject *o, PyObject *key);
    DEFINE_DLSYM_TYPE(PyDictValues_Type);
    DEFINE_DLSYM_TYPE(PyBytesIter_Type);
    DEFINE_DLSYM(PyObject *, PyObject_GetIter, PyObject *);
    DEFINE_DLSYM_TYPE(PyTextIOBase_Type);
    DEFINE_DLSYM(PyObject *, PyWeakref_GetObject, PyObject *ref);
    DEFINE_DLSYM(Py_UCS4*, PyUnicode_AsUCS4Copy, PyObject *unicode);
    DEFINE_DLSYM(unsigned long, PyThread_get_thread_native_id, void);
    DEFINE_DLSYM(PyObject *, PyMemoryView_FromMemory, char *mem, Py_ssize_t size,int flags);
    DEFINE_DLSYM(long, PyMarshal_ReadLongFromFile, FILE *);
    DEFINE_DLSYM(void, PyOS_BeforeFork, void);
    DEFINE_DLSYM(PyVarObject *, PyObject_InitVar, PyVarObject *,PyTypeObject *, Py_ssize_t);
    DEFINE_DLSYM(int, PyList_Append, PyObject *, PyObject *);
    DEFINE_DLSYM(PyThreadState *, PyThreadState_Swap, PyThreadState *);
    DEFINE_DLSYM(int, PyObject_GetBuffer, PyObject *obj, Py_buffer *view,int flags);
    DEFINE_DLSYM(Py_ssize_t, PyNumber_AsSsize_t, PyObject *o, PyObject *exc);
    DEFINE_DLSYM(unsigned long, PyThread_get_thread_ident, void);
    DEFINE_DLSYM(int, PyMember_SetOne, char *, struct PyMemberDef *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyErr_FormatV,PyObject *exception,const char *format,va_list vargs);
    DEFINE_DLSYM(void, Py_ExitStatusException, PyStatus err);
    DEFINE_DLSYM(void, PyErr_SyntaxLocationObject,PyObject *filename,int lineno,int col_offset);
    DEFINE_DLSYM(PyObject *, PyEval_GetGlobals, void);
    DEFINE_DLSYM(int, PyMapping_Check, PyObject *o);
    DEFINE_DLSYM(int, PyObject_DelItemString, PyObject *o, const char *key);
    DEFINE_DLSYM(void, PyUnicode_Append,PyObject **pleft,           /* Pointer to left string */PyObject *right             /* Right string */);
    DEFINE_DLSYM(int, Py_ReprEnter, PyObject *);
    DEFINE_DLSYM(int, PyMapping_HasKeyString, PyObject *o, const char *key);
    DEFINE_DLSYM(void, Py_GetArgcArgv, int *argc, wchar_t ***argv);
    DEFINE_DLSYM(PyObject *, PyObject_CallFunctionObjArgs, PyObject *callable,...);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeCharmap,const char *string,         /* Encoded string */Py_ssize_t length,          /* size of string */PyObject *mapping,          /* decoding mapping */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PyImport_ImportModuleNoBlock,const char *name            /* UTF-8 encoded string */);
    DEFINE_DLSYM_TYPE(PyGetSetDescr_Type);
    DEFINE_DLSYM(PyObject *, PyModule_NewObject,PyObject *name);
    DEFINE_DLSYM(double, PyFloat_GetMin, void);
    DEFINE_DLSYM(wchar_t*, PyUnicode_AsWideCharString,PyObject *unicode,          /* Unicode object */Py_ssize_t *size            /* number of characters of the result */);
    DEFINE_DLSYM(int, PySequence_Check, PyObject *o);
    DEFINE_DLSYM(void, PyBytes_Concat, PyObject **, PyObject *);
    DEFINE_DLSYM(PyObject *, PyDescr_NewClassMethod, PyTypeObject *, PyMethodDef *);
    DEFINE_DLSYM_TYPE(PyFileIO_Type);
    DEFINE_DLSYM(PyObject *, PyUnicodeDecodeError_GetObject, PyObject *);
    DEFINE_DLSYM(void, PyEval_AcquireThread, PyThreadState *tstate);
    DEFINE_DLSYM(PyStatus, PyStatus_NoMemory, void);
    DEFINE_DLSYM(PyObject *, PyNumber_Power, PyObject *o1, PyObject *o2,PyObject *o3);
    DEFINE_DLSYM(PyObject *, PyErr_SetFromErrnoWithFilename,PyObject *exc,const char *filename   /* decoded from the filesystem encoding */);
    DEFINE_DLSYM_TYPE(PyTuple_Type);
    DEFINE_DLSYM(void *, PyCapsule_GetPointer, PyObject *capsule, const char *name);
    DEFINE_DLSYM(int, PyUnicodeEncodeError_GetEnd, PyObject *, Py_ssize_t *);
    DEFINE_DLSYM(PyObject *, PyErr_ProgramText,const char *filename,       /* decoded from the filesystem encoding */int lineno);
    DEFINE_DLSYM(PyObject *, PyDict_GetItemString, PyObject *dp, const char *key);
    DEFINE_DLSYM(void, PyFrame_LocalsToFast, PyFrameObject *, int);
    DEFINE_DLSYM(PyObject *, PyNumber_TrueDivide, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(int, PyFunction_SetKwDefaults, PyObject *, PyObject *);
    DEFINE_DLSYM(void, PyObject_GC_UnTrack, void *);
    DEFINE_DLSYM(PyObject*, PyUnicode_EncodeUTF16,PyObject* unicode,          /* Unicode object */const char *errors,         /* error handling */int byteorder               /* byteorder to use 0=BOM+native;-1=LE,1=BE */);
    DEFINE_DLSYM(void, PyFrame_FastToLocals, PyFrameObject *);
    DEFINE_DLSYM(PyObject *, PyNumber_Absolute, PyObject *o);
    DEFINE_DLSYM(int64_t, PyInterpreterState_GetID, PyInterpreterState *);
    DEFINE_DLSYM(int, PyFunction_SetClosure, PyObject *, PyObject *);
    DEFINE_DLSYM(int, PySys_SetObject, const char *, PyObject *);
    DEFINE_DLSYM(PyOS_sighandler_t, PyOS_setsig, int, PyOS_sighandler_t);
    DEFINE_DLSYM(int, PyFrame_GetLineNumber, PyFrameObject *);
    DEFINE_DLSYM(PyObject *, PyGen_NewWithQualName, PyFrameObject *,PyObject *name, PyObject *qualname);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeRawUnicodeEscape,const char *string,         /* Raw-Unicode-Escape encoded string */Py_ssize_t length,          /* size of string */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PyNumber_Divmod, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject*, PyUnicode_FromStringAndSize,const char *u,             /* UTF-8 encoded string */Py_ssize_t size            /* size of buffer */);
    DEFINE_DLSYM(int, PyObject_GenericSetDict, PyObject *, PyObject *, void *);
    DEFINE_DLSYM(void, PyEval_SetTrace, Py_tracefunc, PyObject *);
    DEFINE_DLSYM(int, PyCallable_Check, PyObject *);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceSubtract, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(unsigned long, PyLong_AsUnsignedLongMask, PyObject *);
    DEFINE_DLSYM(PyInterpreterState *, PyInterpreterState_Head, void);
    DEFINE_DLSYM_TYPE(PyEnum_Type);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF7,const char *string,         /* UTF-7 encoded string */Py_ssize_t length,          /* size of string */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF8,const char *string,         /* UTF-8 encoded string */Py_ssize_t length,          /* size of string */const char *errors          /* error handling */);
    DEFINE_DLSYM(int, PyObject_SetAttrString, PyObject *, const char *, PyObject *);
    DEFINE_DLSYM(int, Py_SetStandardStreamEncoding, const char *encoding,const char *errors);
    DEFINE_DLSYM(int, PyContextVar_Reset, PyObject *var, PyObject *token);
    DEFINE_DLSYM(int, PyImport_AppendInittab,const char *name,           /* ASCII encoded string */PyObject* (*initfunc)(void));
    DEFINE_DLSYM(void, PyThread_tss_free, Py_tss_t *key);
    DEFINE_DLSYM(int, PySet_Clear, PyObject *set);
    DEFINE_DLSYM(int, PyThread_acquire_lock, PyThread_type_lock, int);
    DEFINE_DLSYM(int, PyUnicode_Compare,PyObject *left,             /* Left string */PyObject *right             /* Right string */);
    DEFINE_DLSYM(PyObject *, PyFunction_GetAnnotations, PyObject *);
    DEFINE_DLSYM(const Py_UNICODE *, PyUnicode_AsUnicode,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM(PyCodeObject *, PyCode_New,int, int, int, int, int, PyObject *, PyObject *,PyObject *, PyObject *, PyObject *, PyObject *,PyObject *, PyObject *, int, PyObject *);
    DEFINE_DLSYM_TYPE(PyCallIter_Type);
    DEFINE_DLSYM(PyObject *, PyErr_SetFromErrnoWithFilenameObject,PyObject *, PyObject *);
    DEFINE_DLSYM(int, PySet_Contains, PyObject *anyset, PyObject *key);
    DEFINE_DLSYM(int, PySlice_GetIndicesEx, PyObject *r, Py_ssize_t length,Py_ssize_t *start, Py_ssize_t *stop,Py_ssize_t *step,Py_ssize_t *slicelength);
    DEFINE_DLSYM(const char *, Py_GetCompiler, void);
    DEFINE_DLSYM(PyObject *, Py_CompileString, const char *, const char *, int);
    DEFINE_DLSYM(PyObject*, PyUnicode_FromString,const char *u              /* UTF-8 encoded string */);
    DEFINE_DLSYM(PyObject *, PyObject_Repr, PyObject *);
    DEFINE_DLSYM(PyObject*, PyState_FindModule, struct PyModuleDef*);
    DEFINE_DLSYM(PyObject *, PyMethod_Function, PyObject *);
    DEFINE_DLSYM(void, PyErr_SetObject, PyObject *, PyObject *);
    DEFINE_DLSYM(int, PyEval_MergeCompilerFlags, PyCompilerFlags *cf);
    DEFINE_DLSYM(PyObject *, PyObject_CallObject, PyObject *callable,PyObject *args);
    DEFINE_DLSYM_TYPE(PyStdPrinter_Type);
    DEFINE_DLSYM_TYPE(PyDictIterValue_Type);
    DEFINE_DLSYM_TYPE(PyIOBase_Type);
    DEFINE_DLSYM(PyObject*, PyUnicode_FromKindAndData,int kind,const void *buffer,Py_ssize_t size);
    DEFINE_DLSYM(PyObject *, PyCodec_StreamReader,const char *encoding,PyObject *stream,const char *errors);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeFSDefaultAndSize,const char *s,               /* encoded string */Py_ssize_t size              /* size */);
    DEFINE_DLSYM(int, PyDict_Contains, PyObject *mp, PyObject *key);
    DEFINE_DLSYM(PyObject*, PyUnicode_Substring,PyObject *str,Py_ssize_t start,Py_ssize_t end);
    DEFINE_DLSYM(PyObject *, PyNumber_Or, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject*, PyUnicode_RSplit,PyObject *s,                /* String to split */PyObject *sep,              /* String separator */Py_ssize_t maxsplit         /* Maxsplit count */);
    DEFINE_DLSYM(int, PyRun_InteractiveOneObject,FILE *fp,PyObject *filename,PyCompilerFlags *flags);
    DEFINE_DLSYM_TYPE(PyMemberDescr_Type);
    DEFINE_DLSYM_TYPE(PyDictRevIterValue_Type);
    DEFINE_DLSYM(void, Py_DecRef, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF32Stateful,const char *string,         /* UTF-32 encoded string */Py_ssize_t length,          /* size of string */const char *errors,         /* error handling */int *byteorder,             /* pointer to byteorder to use0=native;-1=LE,1=BE; updated onexit */Py_ssize_t *consumed        /* bytes consumed */);
    DEFINE_DLSYM(int, PyRun_SimpleFileExFlags,FILE *fp,const char *filename,       /* decoded from the filesystem encoding */int closeit,PyCompilerFlags *flags);
    DEFINE_DLSYM(PyObject *, PyMethod_New, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject*, PyType_FromSpec, PyType_Spec*);
    DEFINE_DLSYM(PyObject*, PyUnicode_EncodeUTF32,PyObject *object,           /* Unicode object */const char *errors,         /* error handling */int byteorder               /* byteorder to use 0=BOM+native;-1=LE,1=BE */);
    DEFINE_DLSYM(void, PyConfig_InitIsolatedConfig, PyConfig *config);
    DEFINE_DLSYM(void, Py_SetProgramName, const wchar_t *);
    DEFINE_DLSYM(int, PyUnicodeTranslateError_GetEnd, PyObject *, Py_ssize_t *);
    DEFINE_DLSYM(PyObject *, PyBytes_FromString, const char *);
    DEFINE_DLSYM(int, PyUnicodeTranslateError_SetStart, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(int, Py_FinalizeEx, void);
    DEFINE_DLSYM(PyObject *, PyImport_GetModuleDict, void);
    DEFINE_DLSYM(int, PySys_HasWarnOptions, void);
    DEFINE_DLSYM(void, PyUnicode_InternInPlace, PyObject **);
    DEFINE_DLSYM(int, PyModule_AddIntConstant, PyObject *, const char *, long);
    DEFINE_DLSYM(Py_ssize_t, PyObject_LengthHint, PyObject *o, Py_ssize_t);
    DEFINE_DLSYM_TYPE(PyCapsule_Type);
    DEFINE_DLSYM(int, PyContext_Exit, PyObject *);
    DEFINE_DLSYM(PyStatus, PyConfig_SetString,PyConfig *config,wchar_t **config_str,const wchar_t *str);
    DEFINE_DLSYM(PyStatus, PyConfig_SetBytesArgv,PyConfig *config,Py_ssize_t argc,char * const *argv);
    DEFINE_DLSYM(PyObject *, PyEval_EvalFrameEx, PyFrameObject *f, int exc);
    DEFINE_DLSYM(int, PyUnicodeEncodeError_SetEnd, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(int, PyIter_Check, PyObject *);
    DEFINE_DLSYM(int, PyState_AddModule, PyObject*, struct PyModuleDef*);
    DEFINE_DLSYM(void, PySys_AddWarnOptionUnicode, PyObject *);
    DEFINE_DLSYM(PyObject *, PyContext_Copy, PyObject *);
    DEFINE_DLSYM_TYPE(PyByteArrayIter_Type);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeFSDefault,const char *s               /* encoded string */);
    DEFINE_DLSYM(PyObject *, PyStaticMethod_New, PyObject *);
    DEFINE_DLSYM(int, PyCell_Set, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyContextVar_New,const char *name, PyObject *default_value);
    DEFINE_DLSYM(PyObject *, PyNumber_MatrixMultiply, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject *, PyCoro_New, PyFrameObject *,PyObject *name, PyObject *qualname);
    DEFINE_DLSYM(int, PyRun_SimpleStringFlags, const char *, PyCompilerFlags *);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_GetLength,PyObject *unicode);
    DEFINE_DLSYM(int, PyModule_AddStringConstant, PyObject *, const char *, const char *);
    DEFINE_DLSYM(PyObject *, PyAsyncGen_New, PyFrameObject *,PyObject *name, PyObject *qualname);
    DEFINE_DLSYM(PyStatus, PyWideStringList_Insert, PyWideStringList *list,Py_ssize_t index,const wchar_t *item);
    DEFINE_DLSYM_TYPE(PyCode_Type);
    DEFINE_DLSYM_TYPE(PyProperty_Type);
    DEFINE_DLSYM(PyObject *, PyException_GetContext, PyObject *);
    DEFINE_DLSYM(PyObject *, PyEval_GetBuiltins, void);
    DEFINE_DLSYM(int, PyMapping_HasKey, PyObject *o, PyObject *key);
    DEFINE_DLSYM(PyObject *, PyImport_ImportModuleLevel,const char *name,           /* UTF-8 encoded string */PyObject *globals,PyObject *locals,PyObject *fromlist,int level);
    DEFINE_DLSYM(Py_ssize_t, PyGC_Collect, void);
    DEFINE_DLSYM_TYPE(PyRange_Type);
    DEFINE_DLSYM(PyTypeObject*, PyStructSequence_NewType, PyStructSequence_Desc *desc);
    DEFINE_DLSYM(void, PyThread_exit_thread, void);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_Count,PyObject *str,              /* String */PyObject *substr,           /* Substring to count */Py_ssize_t start,           /* Start index */Py_ssize_t end              /* Stop index */);
    DEFINE_DLSYM(void, PyThread_init_thread, void);
    DEFINE_DLSYM(PyObject *, PyErr_SetImportError, PyObject *, PyObject *,PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_FromEncodedObject,PyObject *obj,              /* Object */const char *encoding,       /* encoding */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyFrameObject *, PyEval_GetFrame, void);
    DEFINE_DLSYM(PyObject *, PyLong_FromUnsignedLong, unsigned long);
    DEFINE_DLSYM(void *, PyMem_RawMalloc, size_t size);
    DEFINE_DLSYM_TYPE(PyBool_Type);
    DEFINE_DLSYM(void, PyFrame_BlockSetup, PyFrameObject *, int, int, int);
    DEFINE_DLSYM_TYPE(PyDictItems_Type);
    DEFINE_DLSYM(PyObject *, PyEval_EvalCodeEx, PyObject *co,PyObject *globals,PyObject *locals,PyObject *const *args, int argc,PyObject *const *kwds, int kwdc,PyObject *const *defs, int defc,PyObject *kwdefs, PyObject *closure);
    DEFINE_DLSYM(wchar_t *, Py_GetProgramFullPath, void);
    DEFINE_DLSYM(int, PyArg_Parse, PyObject *, const char *, ...);
    DEFINE_DLSYM(PyObject *, PyInstanceMethod_Function, PyObject *);
    DEFINE_DLSYM(void, PyObject_Free, void *ptr);
    DEFINE_DLSYM(PyThreadState *, PyGILState_GetThisThreadState, void);
    DEFINE_DLSYM_TYPE(PyUnicodeIter_Type);
    DEFINE_DLSYM(PyObject *, PyUnicodeDecodeError_Create,const char *encoding,       /* UTF-8 encoded string */const char *object,Py_ssize_t length,Py_ssize_t start,Py_ssize_t end,const char *reason          /* UTF-8 encoded string */);
    DEFINE_DLSYM(Py_ssize_t, PyLong_AsSsize_t, PyObject *);
    DEFINE_DLSYM_TYPE(PyComplex_Type);
    DEFINE_DLSYM(PyObject *, PyNumber_And, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void, PyErr_SyntaxLocation,const char *filename,       /* decoded from the filesystem encoding */int lineno);
    DEFINE_DLSYM(PyObject*, PyType_FromSpecWithBases, PyType_Spec*, PyObject*);
    DEFINE_DLSYM(void, PyObject_GC_Del, void *);
    DEFINE_DLSYM(int, PyCode_Addr2Line, PyCodeObject *, int);
    DEFINE_DLSYM(long long, PyLong_AsLongLongAndOverflow, PyObject *, int *);
    DEFINE_DLSYM(int, PyRun_AnyFileExFlags,FILE *fp,const char *filename,       /* decoded from the filesystem encoding */int closeit,PyCompilerFlags *flags);
    DEFINE_DLSYM_TYPE(PyTextIOWrapper_Type);
    DEFINE_DLSYM(unsigned long, PyOS_strtoul, const char *, char **, int);
    DEFINE_DLSYM(PyObject *, PyObject_ASCII, PyObject *);
    DEFINE_DLSYM(PyObject *, PyImport_ReloadModule, PyObject *m);
    DEFINE_DLSYM(PyObject *, PyMapping_Items, PyObject *o);
    DEFINE_DLSYM(PyObject *, PySequence_Fast, PyObject *o, const char* m);
    DEFINE_DLSYM(PyObject *, PyObject_RichCompare, PyObject *, PyObject *, int);
    DEFINE_DLSYM(void, PyObject_GC_Track, void *);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsEncodedString,PyObject *unicode,          /* Unicode object */const char *encoding,       /* encoding */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PyDict_Copy, PyObject *mp);
    DEFINE_DLSYM(PyObject *, PyUnicodeEncodeError_GetObject, PyObject *);
    DEFINE_DLSYM(int, Py_Main, int argc, wchar_t **argv);
    DEFINE_DLSYM(PyObject *, PyModule_GetFilenameObject, PyObject *);
    DEFINE_DLSYM(int, PyFunction_SetDefaults, PyObject *, PyObject *);
    DEFINE_DLSYM(void *, PyLong_AsVoidPtr, PyObject *);
    DEFINE_DLSYM(int, PyUnicodeTranslateError_SetEnd, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(char*, Py_EncodeLocale,const wchar_t *text,size_t *error_pos);
    DEFINE_DLSYM(int, PyDict_SetItemString, PyObject *dp, const char *key, PyObject *item);
    DEFINE_DLSYM(PyObject *, PyCFunction_NewEx, PyMethodDef *, PyObject *,PyObject *);
    DEFINE_DLSYM(PyObject *, PyCodec_ReplaceErrors, PyObject *exc);
    DEFINE_DLSYM(PyObject *, PyErr_NewException,const char *name, PyObject *base, PyObject *dict);
    DEFINE_DLSYM(PyObject *, PyDict_GetItemWithError, PyObject *mp, PyObject *key);
    DEFINE_DLSYM_TYPE(PyODict_Type);
    DEFINE_DLSYM(void, PyStructSequence_SetItem, PyObject*, Py_ssize_t, PyObject*);
    DEFINE_DLSYM(PyObject *, PyUnicode_Replace,PyObject *str,              /* String */PyObject *substr,           /* Substring to find */PyObject *replstr,          /* Substring to replace */Py_ssize_t maxcount         /* Max. number of replacements to apply;-1 = all */);
    DEFINE_DLSYM(long long, PyLong_AsLongLong, PyObject *);
    DEFINE_DLSYM(void, PyErr_NormalizeException, PyObject**, PyObject**, PyObject**);
    DEFINE_DLSYM(PyObject *, PyList_GetSlice, PyObject *, Py_ssize_t, Py_ssize_t);
    DEFINE_DLSYM(unsigned long, PyLong_AsUnsignedLong, PyObject *);
    DEFINE_DLSYM(PyThreadState *, PyThreadState_Get, void);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsUTF8String,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM_TYPE(PyFilter_Type);
    DEFINE_DLSYM(void, PyErr_WriteUnraisable, PyObject *);
    DEFINE_DLSYM_TYPE(PyODictItems_Type);
    DEFINE_DLSYM_TYPE(PyBaseObject_Type);
    DEFINE_DLSYM(PyObject *, PySlice_New, PyObject* start, PyObject* stop,PyObject* step);
    DEFINE_DLSYM(PyObject *, PyFunction_GetDefaults, PyObject *);
    DEFINE_DLSYM(int, PyFrame_FastToLocalsWithError, PyFrameObject *f);
    DEFINE_DLSYM(PyObject *, PyMethod_Self, PyObject *);
    DEFINE_DLSYM(void*, PyType_GetSlot, PyTypeObject*, int);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_AsWideChar,PyObject *unicode,          /* Unicode object */wchar_t *w,                 /* wchar_t buffer */Py_ssize_t size             /* size of buffer */);
    DEFINE_DLSYM(int, PyObject_AsCharBuffer, PyObject *obj,const char **buffer,Py_ssize_t *buffer_len);
    DEFINE_DLSYM(void, PyBuffer_Release, Py_buffer *view);
    DEFINE_DLSYM(PyObject *, PyObject_GetAttrString, PyObject *, const char *);
    DEFINE_DLSYM(PyObject *, PyDescr_NewMember, PyTypeObject *,struct PyMemberDef *);
    DEFINE_DLSYM(int, PyList_Reverse, PyObject *);
    DEFINE_DLSYM(PyObject *, PyEval_EvalFrame, PyFrameObject *);
    DEFINE_DLSYM_TYPE(PyContext_Type);
    DEFINE_DLSYM(int, PyCodec_KnownEncoding,const char *encoding);
    DEFINE_DLSYM(PyObject *, PyDict_SetDefault,PyObject *mp, PyObject *key, PyObject *defaultobj);
    DEFINE_DLSYM(PyObject*, PyUnicode_FromObject,PyObject *obj      /* Object */);
    DEFINE_DLSYM(PyObject *, PyMarshal_ReadObjectFromFile, FILE *);
    DEFINE_DLSYM(double, PyOS_string_to_double, const char *str,char **endptr,PyObject *overflow_exception);
    DEFINE_DLSYM(void, PyMarshal_WriteLongToFile, long, FILE *, int);
    DEFINE_DLSYM(PyObject*, PyUnicode_Decode,const char *s,              /* encoded string */Py_ssize_t size,            /* size of buffer */const char *encoding,       /* encoding */const char *errors          /* error handling */);
    DEFINE_DLSYM(int, PyObject_SetItem, PyObject *o, PyObject *key, PyObject *v);
    DEFINE_DLSYM(void, PySys_SetArgvEx, int, wchar_t **, int);
    DEFINE_DLSYM(int, PyType_IsSubtype, PyTypeObject *, PyTypeObject *);
    DEFINE_DLSYM(int, PyRun_SimpleFile, FILE *f, const char *p);
    DEFINE_DLSYM(const char *, Py_GetVersion, void);
    DEFINE_DLSYM(PyObject *, PyNumber_Float, PyObject *o);
    DEFINE_DLSYM(PyObject *, PyCallIter_New, PyObject *, PyObject *);
    DEFINE_DLSYM(int, Py_MakePendingCalls, void);
    DEFINE_DLSYM(void, PyEval_ReleaseThread, PyThreadState *tstate);
    DEFINE_DLSYM(PyObject *, PyFloat_FromDouble, double);
    DEFINE_DLSYM(PyObject *, PyFunction_GetClosure, PyObject *);
    DEFINE_DLSYM(int, PyCompile_OpcodeStackEffectWithJump, int opcode, int oparg, int jump);
    DEFINE_DLSYM(PyObject *, PyErr_SetImportErrorSubclass, PyObject *, PyObject *,PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyBytes_FromObject, PyObject *);
    DEFINE_DLSYM(int, Py_RunMain, void);
    DEFINE_DLSYM(PyStatus, Py_PreInitialize,const PyPreConfig *src_config);
    DEFINE_DLSYM(PyObject *, PyFunction_New, PyObject *, PyObject *);
    DEFINE_DLSYM(int, PyBuffer_FillInfo, Py_buffer *view, PyObject *o, void *buf,Py_ssize_t len, int readonly,int flags);
    DEFINE_DLSYM(PyObject *, PyNumber_Index, PyObject *o);
    DEFINE_DLSYM(PyObject *, PyList_AsTuple, PyObject *);
    DEFINE_DLSYM_TYPE(PyDictRevIterKey_Type);
    DEFINE_DLSYM(PyObject *, PyCodec_Encoder,const char *encoding);
    DEFINE_DLSYM(PyThreadState *, PyEval_SaveThread, void);
    DEFINE_DLSYM(PyObject*, PyUnicode_Join,PyObject *separator,        /* Separator string */PyObject *seq               /* Sequence object */);
    DEFINE_DLSYM(wchar_t *, Py_GetPath, void);
    DEFINE_DLSYM(Py_ssize_t, PySequence_Count, PyObject *o, PyObject *value);
    DEFINE_DLSYM(int, PyOS_InterruptOccurred, void);
    DEFINE_DLSYM(int, PyException_SetTraceback, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PySequence_GetSlice, PyObject *o, Py_ssize_t i1, Py_ssize_t i2);
    DEFINE_DLSYM_TYPE(PyDictKeys_Type);
    DEFINE_DLSYM(Py_ssize_t, PyUnicode_FindChar,PyObject *str,Py_UCS4 ch,Py_ssize_t start,Py_ssize_t end,int direction);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsUnicodeEscapeString,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF16,const char *string,         /* UTF-16 encoded string */Py_ssize_t length,          /* size of string */const char *errors,         /* error handling */int *byteorder              /* pointer to byteorder to use0=native;-1=LE,1=BE; updated onexit */);
    DEFINE_DLSYM(PyObject *, PyLong_FromString, const char *, char **, int);
    DEFINE_DLSYM(void, PyType_Modified, PyTypeObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUnicodeEscape,const char *string,         /* Unicode-Escape encoded string */Py_ssize_t length,          /* size of string */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PySequence_InPlaceRepeat, PyObject *o, Py_ssize_t count);
    DEFINE_DLSYM(PyObject *, PySequence_Concat, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceOr, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM_TYPE(PyDict_Type);
    DEFINE_DLSYM(size_t, PyLong_AsSize_t, PyObject *);
    DEFINE_DLSYM(int, Py_FdIsInteractive, FILE *, const char *);
    DEFINE_DLSYM(PyObject *, PyUnicodeDecodeError_GetReason, PyObject *);
    DEFINE_DLSYM(PyObject *, PyNumber_Remainder, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM_TYPE(PySTEntry_Type);
    DEFINE_DLSYM(int, PyOS_mystricmp, const char *, const char *);
    DEFINE_DLSYM(PyObject *, PyMapping_GetItemString, PyObject *o,const char *key);
    DEFINE_DLSYM(int, PyDict_Next,PyObject *mp, Py_ssize_t *pos, PyObject **key, PyObject **value);
    DEFINE_DLSYM(void, PyObject_SetArenaAllocator, PyObjectArenaAllocator *allocator);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceAdd, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(void, Py_Initialize, void);
    DEFINE_DLSYM(int, PyBytes_AsStringAndSize,PyObject *obj,      /* bytes object */char **s,           /* pointer to buffer variable */Py_ssize_t *len     /* pointer to length variable or NULL */);
    DEFINE_DLSYM(int, PyToken_OneChar, int);
    DEFINE_DLSYM(PyObject *, PyCodec_Decode,PyObject *object,const char *encoding,const char *errors);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeLocale,const char *str,const char *errors);
    DEFINE_DLSYM(PyObject *, PyNumber_InPlaceMatrixMultiply, PyObject *o1, PyObject *o2);
    DEFINE_DLSYM(int, PyODict_DelItem, PyObject *od, PyObject *key);
    DEFINE_DLSYM(int, PyObject_CopyData, PyObject *dest, PyObject *src);
    DEFINE_DLSYM_TYPE(PySet_Type);
    DEFINE_DLSYM(double, PyFloat_AsDouble, PyObject *);
    DEFINE_DLSYM(PyObject *, PyUnicode_Translate,PyObject *str,              /* String */PyObject *table,            /* Translate table */const char *errors          /* error handling */);
    DEFINE_DLSYM(PyObject *, PyLong_FromLongLong, long long);
    DEFINE_DLSYM(void, PyErr_SetInterrupt, void);
    DEFINE_DLSYM_TYPE(PyRangeIter_Type);
    DEFINE_DLSYM(void, PyMem_SetupDebugHooks, void);
    DEFINE_DLSYM(int, PyErr_WarnEx,PyObject *category,const char *message,        /* UTF-8 encoded string */Py_ssize_t stack_level);
    DEFINE_DLSYM(double, PyComplex_RealAsDouble, PyObject *op);
    DEFINE_DLSYM(PyObject*, PyUnicode_AsUTF16String,PyObject *unicode           /* Unicode object */);
    DEFINE_DLSYM_TYPE(PyMethod_Type);
    DEFINE_DLSYM_TYPE(PyContextVar_Type);
    DEFINE_DLSYM(PyObject *, PyDescr_NewGetSet, PyTypeObject *,struct PyGetSetDef *);
    DEFINE_DLSYM(unsigned long long, PyLong_AsUnsignedLongLongMask, PyObject *);
    DEFINE_DLSYM(int, PySet_Discard, PyObject *set, PyObject *key);
    DEFINE_DLSYM(PyObject *, PyFile_OpenCode, const char *utf8path);
    DEFINE_DLSYM(PyLockStatus, PyThread_acquire_lock_timed, PyThread_type_lock,PY_TIMEOUT_T microseconds,int intr_flag);
    DEFINE_DLSYM(void, PyErr_Display, PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject*, PyUnicode_DecodeUTF32,const char *string,         /* UTF-32 encoded string */Py_ssize_t length,          /* size of string */const char *errors,         /* error handling */int *byteorder              /* pointer to byteorder to use0=native;-1=LE,1=BE; updated onexit */);
    DEFINE_DLSYM(PyObject*, PyUnicode_FromOrdinal, int ordinal);
    DEFINE_DLSYM(Py_ssize_t, PyByteArray_Size, PyObject *);
    DEFINE_DLSYM(PyObject *, PyCodec_XMLCharRefReplaceErrors, PyObject *exc);
    DEFINE_DLSYM(PyObject *, PyMapping_Keys, PyObject *o);
    DEFINE_DLSYM(int, PyUnicodeEncodeError_GetStart, PyObject *, Py_ssize_t *);
    DEFINE_DLSYM(PyObject *, PyImport_ExecCodeModule,const char *name,           /* UTF-8 encoded string */PyObject *co);
    DEFINE_DLSYM(PyObject *, PyCodec_Encode,PyObject *object,const char *encoding,const char *errors);
    DEFINE_DLSYM_TYPE(PyType_Type);
    DEFINE_DLSYM(PyObject *, PyErr_ProgramTextObject,PyObject *filename,int lineno);
    DEFINE_DLSYM(void, PyErr_SetNone, PyObject *);
    DEFINE_DLSYM(void, PyException_SetCause, PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyRun_String, const char *str, int s, PyObject *g, PyObject *l);
    DEFINE_DLSYM(PyObject *, PyCFunction_New, PyMethodDef *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyClassMethod_New, PyObject *);
    DEFINE_DLSYM_TYPE(PyODictValues_Type);
    DEFINE_DLSYM(int, PyModule_ExecDef, PyObject *module, PyModuleDef *def);
    DEFINE_DLSYM(int, PyTraceMalloc_Track,unsigned int domain,uintptr_t ptr,size_t size);
    DEFINE_DLSYM(int, PyByteArray_Resize, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(PyObject *, PyList_GetItem, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(PyObject *, Py_VaBuildValue, const char *, va_list);
    DEFINE_DLSYM(int, PyObject_Print, PyObject *, FILE *, int);
    DEFINE_DLSYM(char *, PyByteArray_AsString, PyObject *);
    DEFINE_DLSYM(long, PyLong_AsLong, PyObject *);
    DEFINE_DLSYM(int, PyFile_SetOpenCodeHook, Py_OpenCodeHookFunction hook, void *userData);
    DEFINE_DLSYM(PyObject *, PyObject_GenericGetAttr, PyObject *, PyObject *);
    DEFINE_DLSYM(Py_hash_t, PyObject_Hash, PyObject *);
    DEFINE_DLSYM(int, Py_GetRecursionLimit, void);
    DEFINE_DLSYM(void, PyPreConfig_InitPythonConfig, PyPreConfig *config);
    DEFINE_DLSYM(PyInterpreterState *, PyInterpreterState_Main, void);
    DEFINE_DLSYM(PyObject *, PyImport_ImportModule,const char *name            /* UTF-8 encoded string */);
    DEFINE_DLSYM(int, Py_BytesMain, int argc, char **argv);
    DEFINE_DLSYM(PyObject *, PyDescr_NewWrapper, PyTypeObject *,struct wrapperbase *, void *);
    DEFINE_DLSYM(PyObject *, PyImport_ExecCodeModuleObject,PyObject *name,PyObject *co,PyObject *pathname,PyObject *cpathname);
    DEFINE_DLSYM(PyObject *, PyErr_Format,PyObject *exception,const char *format,   /* ASCII-encoded string  */...);
    DEFINE_DLSYM(PyObject *, PySet_New, PyObject *);
    DEFINE_DLSYM(int, PyObject_Not, PyObject *);
    DEFINE_DLSYM(int, PyDict_DelItem, PyObject *mp, PyObject *key);
    DEFINE_DLSYM_TYPE(PyRawIOBase_Type);
    DEFINE_DLSYM(PyStatus, PyConfig_SetWideStringList, PyConfig *config,PyWideStringList *list,Py_ssize_t length, wchar_t **items);
    DEFINE_DLSYM(int, PyUnicodeDecodeError_SetStart, PyObject *, Py_ssize_t);
    DEFINE_DLSYM(PyObject *, PyFunction_GetModule, PyObject *);
    DEFINE_DLSYM(const char *, PyUnicode_AsUTF8, PyObject *unicode);
    DEFINE_DLSYM(PyObject*, PyUnicode_RPartition,PyObject *s,                /* String to partition */PyObject *sep               /* String separator */);
    DEFINE_DLSYM(PyObject *, PyNumber_Invert, PyObject *o);
    DEFINE_DLSYM(void, PyObject_ClearWeakRefs, PyObject *);
    DEFINE_DLSYM_TYPE(PyBufferedWriter_Type);
    DEFINE_DLSYM(PyObject *, PyFile_NewStdPrinter, int);
    DEFINE_DLSYM(PyObject *, PyVectorcall_Call, PyObject *callable, PyObject *tuple, PyObject *dict);
    DEFINE_DLSYM_TYPE(PyContextTokenMissing_Type);
    DEFINE_DLSYM(PyObject *, PyContextVar_Set, PyObject *var, PyObject *value);
    DEFINE_DLSYM_TYPE(PyBytes_Type);
    DEFINE_DLSYM(PyObject *, PyUnicode_FromFormatV,const char *format,   /* ASCII-encoded string  */va_list vargs);
    DEFINE_DLSYM(void, PyErr_BadInternalCall, void);
    DEFINE_DLSYM(int, PyUnicodeTranslateError_SetReason,PyObject *exc,const char *reason          /* UTF-8 encoded string */);
    DEFINE_DLSYM(int, PyDict_MergeFromSeq2, PyObject *d,PyObject *seq2,int override);
    DEFINE_DLSYM(PyFrameObject *, PyFrame_New, PyThreadState *, PyCodeObject *,PyObject *, PyObject *);
    DEFINE_DLSYM(PyObject *, PyFunction_NewWithQualName, PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM(void *, PyMem_RawCalloc, size_t nelem, size_t elsize);
    DEFINE_DLSYM(PyObject *, PyCodec_IncrementalEncoder,const char *encoding,const char *errors);
    DEFINE_DLSYM(PyObject *, PyStructSequence_New, PyTypeObject* type);
    DEFINE_DLSYM(int, PyODict_SetItem, PyObject *od, PyObject *key, PyObject *item);
    DEFINE_DLSYM(PyObject*, PyUnicode_TransformDecimalToASCII,Py_UNICODE *s,              /* Unicode buffer */Py_ssize_t length           /* Number of Py_UNICODE chars to transform */);
    DEFINE_DLSYM(const char *, PyImport_GetMagicTag, void);
    DEFINE_DLSYM(PyObject *, PyEval_GetLocals, void);
    DEFINE_DLSYM(int, PyImport_ExtendInittab, struct _inittab *newtab);
    DEFINE_DLSYM(PyObject *, PyRun_File, FILE *fp, const char *p, int s, PyObject *g, PyObject *l);
    DEFINE_DLSYM(PyObject *, PyRun_FileFlags, FILE *fp, const char *p, int s, PyObject *g, PyObject *l, PyCompilerFlags *flags);
    DEFINE_DLSYM(void, PyMem_Free, void *ptr);
    DEFINE_DLSYM(PyObject *, PyErr_NoMemory, void);
    DEFINE_DLSYM(int, PyCompile_OpcodeStackEffect, int opcode, int oparg);
    DEFINE_DLSYM(int, PyFunction_SetAnnotations, PyObject *, PyObject *);
    DEFINE_DLSYM_TYPE(PyBufferedRWPair_Type);
    DEFINE_DLSYM(PyObject *, PyThreadState_GetDict, void);
    DEFINE_DLSYM(PyObject *, PyWeakref_NewProxy, PyObject *ob,PyObject *callback);
    DEFINE_DLSYM(Py_ssize_t, PySlice_AdjustIndices, Py_ssize_t length,Py_ssize_t *start, Py_ssize_t *stop,Py_ssize_t step);
    DEFINE_DLSYM(PyObject *, PyModuleDef_Init, struct PyModuleDef*);
    DEFINE_DLSYM(PyObject *, PyModule_Create2, struct PyModuleDef*,int apiver);
    DEFINE_DLSYM(void, PyMem_RawFree, void *ptr);
    DEFINE_DLSYM(long, PyLong_AsLongAndOverflow, PyObject *, int *);
    DEFINE_DLSYM(PyThreadState *, PyThreadState_Next, PyThreadState *);
    DEFINE_DLSYM(Py_ssize_t, PyMapping_Size, PyObject *o);
    DEFINE_DLSYM_TYPE(PyTupleIter_Type);
    DEFINE_DLSYM(PyObject *, PyTuple_GetSlice, PyObject *, Py_ssize_t, Py_ssize_t);
    DEFINE_DLSYM_TYPE(PyDictRevIterItem_Type);
    DEFINE_DLSYM(PyObject *, PyCodec_IgnoreErrors, PyObject *exc);
    DEFINE_DLSYM(PyObject *, PyFile_GetLine, PyObject *, int);
    DEFINE_DLSYM(void, PyMem_GetAllocator, PyMemAllocatorDomain domain,PyMemAllocatorEx *allocator);
    DEFINE_DLSYM(void, PyErr_SetExcInfo, PyObject *, PyObject *, PyObject *);
    DEFINE_DLSYM_TYPE(PyFrame_Type);
    DEFINE_DLSYM(int, PyTraceBack_Here, PyFrameObject *);
    DEFINE_DLSYM(PyObject *, PyImport_AddModuleObject,PyObject *name);
    DEFINE_DLSYM(void, PySys_AddWarnOption, const wchar_t *);
    DEFINE_DLSYM_TYPE(PySlice_Type);
    DEFINE_DLSYM(PyObject *, PyCell_Get, PyObject *);
    DEFINE_DLSYM(int, PyGILState_Check, void);
    DEFINE_DLSYM(int, PyArg_UnpackTuple, PyObject *, const char *, Py_ssize_t, Py_ssize_t, ...);
    DEFINE_DLSYM(PyObject *, PyUnicodeEncodeError_GetReason, PyObject *);
    DEFINE_DLSYM(int, PyRun_InteractiveOne, FILE *f, const char *p);
    DEFINE_DLSYM(int, PyMapping_SetItemString, PyObject *o, const char *key,PyObject *value);
    DEFINE_DLSYM(PyObject *, Py_CompileStringObject,const char *str,PyObject *filename, int start,PyCompilerFlags *flags,int optimize);
    DEFINE_DLSYM(PyObject *, PyLong_FromUnsignedLongLong, unsigned long long);
    DEFINE_DLSYM(int, PyThread_tss_create, Py_tss_t *key);
    DEFINE_DLSYM(int, PyObject_AsFileDescriptor, PyObject *);
    DEFINE_DLSYM(int, PyImport_ImportFrozenModule,const char *name            /* UTF-8 encoded string */);
    DEFINE_DLSYM(PyObject *, PyException_GetTraceback, PyObject *);
    DEFINE_DLSYM(Py_ssize_t, PyObject_Size, PyObject *o);
    DEFINE_DLSYM(void, PyErr_Fetch, PyObject **, PyObject **, PyObject **);
    DEFINE_DLSYM_TYPE(PySeqIter_Type);
    DEFINE_DLSYM(PyObject *, PyModule_GetDict, PyObject *);
    DEFINE_DLSYM(int, PyArg_ParseTupleAndKeywords, PyObject *, PyObject *,const char *, char **, ...);
    DEFINE_DLSYM(int, PyDict_Merge, PyObject *mp,PyObject *other,int override);
    DEFINE_DLSYM(void *, PyMem_Malloc, size_t size);
    DEFINE_DLSYM(void, PyBytes_ConcatAndDel, PyObject **, PyObject *);
    DEFINE_DLSYM(PyObject *, PyByteArray_Concat, PyObject *, PyObject *);
    DEFINE_DLSYM(int, PyUnicode_WriteChar,PyObject *unicode,Py_ssize_t index,Py_UCS4 character);
    DEFINE_DLSYM(int, PyThread_tss_set, Py_tss_t *key, void *value);
    DEFINE_DLSYM_TYPE(PyIncrementalNewlineDecoder_Type);
    DEFINE_DLSYM(struct PyModuleDef*, PyModule_GetDef, PyObject*);
    DEFINE_DLSYM(int, PyTuple_SetItem, PyObject *, Py_ssize_t, PyObject *);
    DEFINE_DLSYM(int, PyRun_AnyFileEx, FILE *fp, const char *name, int closeit);
    DEFINE_DLSYM(PyStatus, Py_PreInitializeFromBytesArgs,const PyPreConfig *src_config,Py_ssize_t argc,char **argv);
    DEFINE_DLSYM(int, PyDict_SetItem, PyObject *mp, PyObject *key, PyObject *item);
    DEFINE_DLSYM(PyObject *, PyOS_FSPath, PyObject *path);
    DEFINE_DLSYM(const char *, Py_GetBuildInfo, void);
    DEFINE_DLSYM(PyObject *, PyIter_Next, PyObject *);
    DEFINE_DLSYM(int, PyObject_IsInstance, PyObject *object, PyObject *typeorclass);
    DEFINE_DLSYM(void *, PyMem_RawRealloc, void *ptr, size_t new_size);
    DEFINE_DLSYM(int, Py_IsInitialized, void);
    DEFINE_DLSYM(const char*, PyUnicode_GetDefaultEncoding, void);
    DEFINE_DLSYM_TYPE(PyAsyncGen_Type);
    DEFINE_DLSYM(void, Py_EndInterpreter, PyThreadState *);
    DEFINE_DLSYM(int, PyErr_CheckSignals, void);
    DEFINE_DLSYM(int, PyObject_HasAttrString, PyObject *, const char *);
    DEFINE_DLSYM(PyObject *, PyImport_GetImporter, PyObject *path);
    DEFINE_DLSYM(int, PyRun_SimpleFileEx, FILE *f, const char *p, int c);
    DEFINE_DLSYM(void, PyGILState_Release, PyGILState_STATE);
}

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
}
