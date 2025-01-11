#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <pthread.h>
#include <unistd.h>

#include <windows.h>
#include <psapi.h>
#include <memoryapi.h>

FILE *log_file = NULL;
#define LOG(...){ \
	if(log_file == NULL){ \
		log_file = fopen("./project_torque_hot_patch.log", "w"); \
	} \
	fprintf(log_file, __VA_ARGS__); \
	fflush(log_file); \
}

static void log_buffer(uint8_t *buf, size_t size){
	for(size_t i = 0;i < size;i++){
		LOG("0x%02x ", buf[i]);
	}
}

static void patch(uint32_t location, uint8_t *buf, size_t size){
	DWORD old_protect;
	VirtualProtect((void *)location, size, PAGE_EXECUTE_READWRITE, &old_protect);
	memcpy((void *)location, buf, size);
	VirtualProtect((void *)location, size, old_protect, &old_protect);
}

static void find_and_patch(void *begin, size_t size, uint8_t *target, uint8_t *patch_buf, size_t patch_size, uint32_t first_time, uint32_t times){
	uint32_t i = 0;
	for(uint32_t offset = 0;offset <= size - patch_size;offset++){
		if(memcmp((void *)(offset + (uint32_t)begin), target, patch_size) == 0){
			if(i >= first_time && i < times + first_time){
				patch(offset + (uint32_t)begin, patch_buf, patch_size);
				LOG("0x%08x: ", offset + (uint32_t)begin);
				log_buffer(target, patch_size);
				LOG("-> ");
				log_buffer(patch_buf, patch_size);
				LOG("\n");
			}
			if(i >= times + first_time){
				break;
			}
			i++;
		}
	}
}

static void patch(void *begin, size_t size){
	uint8_t target[] = {0xd9, 0x45, 0xc4, 0xd9, 0xc9, 0xdf, 0xf1, 0xdd, 0xd8, 0x76, 0x07};
	uint8_t patch_buf[] = {0xd9, 0x45, 0xc4, 0xd9, 0xc9, 0xdf, 0xf1, 0xdd, 0xd8, 0xeb, 0x07};
	find_and_patch(begin, size, target, patch_buf, sizeof(target), 0, 1);
}

void *patch_thread(void *arg){
	LOG("%s: begins\n", __func__);

	while(true){
		sleep(2);

		LOG("enumerating modules:\n");

		HMODULE modules[1024];
		DWORD num_modules;
		int ret = EnumProcessModulesEx(GetCurrentProcess(), modules, sizeof(modules), &num_modules, LIST_MODULES_ALL);
		if(ret == 0){
			LOG("failed enumerating modules, %x\n", GetLastError());
			continue;
		}
		for(int i = 0;i<num_modules/sizeof(HMODULE);i++){
			char module_name[1024] = {0};
			ret = GetModuleBaseNameA(GetCurrentProcess(), modules[i], module_name, sizeof(module_name));
			if(ret == 0){
				LOG("faield fetching module name %d, %x\n", i, GetLastError());
				continue;
			}
			MODULEINFO info;
			ret = GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info));
			if(ret == 0){
				LOG("failed fetching module information %d, %x\n", i, GetLastError());
				continue;
			}
			LOG("module %s at 0x%08x\n", module_name, info.lpBaseOfDll);

			if(strcmp("CRC_ReleaseNoDebug.dll", module_name) == 0){
				sleep(3);
				LOG("target found\n");

				patch(info.lpBaseOfDll, info.SizeOfImage);
				// what the hell is this
				patch((void *)0x0BE10000, info.SizeOfImage);
				return NULL;
			}
		}

	}

	return NULL;
}

__attribute__((constructor))
int patch(){
	LOG("%s: begins\n", __func__);

	pthread_t t;
	pthread_create(&t, NULL, patch_thread, NULL);
	pthread_detach(t);

	return 0;
}
