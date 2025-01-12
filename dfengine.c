#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>

#include <windows.h>

#if 1
FILE *log_file = NULL;
#define LOG(...){ \
        if(log_file == NULL){ \
                log_file = fopen("./dfengine_asi_loader_log.txt", "w"); \
        } \
        fprintf(log_file, __VA_ARGS__); \
        fflush(log_file); \
}
#else
#define LOG(...)
#endif

// default call convention?

void * (*GetDFEngine_)();
void * GetDFEngine(){
	if(GetDFEngine_){
		void *ret = GetDFEngine_();
		LOG("%s: Forwarding result with 0x%016x\n", __func__, ret);
		return ret;
	}
	LOG("%s: Returning NULL\n", __func__);
	return NULL;
}

__attribute__((constructor))
int init(){
	LOG("%s: begin\n", __func__);

	HMODULE lib = LoadLibraryA("./realdfengine.dll");
	if(lib == NULL){
		LOG("%s: cannot load RealDFEngine.dll\n", __func__);
	}else{
		GetDFEngine_ = (void * (*)())GetProcAddress(lib, "GetDFEngine");
		if(GetDFEngine_ == NULL){
			LOG("%s: cannot fetch GetDFEngine from RealDFEngine.dll\n", __func__);
		}
	}

	DIR *curdir = opendir("./");
	struct dirent *entry = readdir(curdir);
	while(entry != NULL){
		const char *name = entry->d_name;
		int len = strlen(name);
		if(len > 4){
			const char *ext = &name[len - 4];
			if(strcmp(ext, ".asi") == 0){
				lib = LoadLibraryA(name);
				if(lib != NULL){
					LOG("%s: successfully loaded asi %s\n", __func__, name);
				}else{
					LOG("%s: failed loading asi %s, %x\n", __func__, name, GetLastError());
				}
			}
		}
		entry = readdir(curdir);
	}
}
