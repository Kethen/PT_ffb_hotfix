#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <pthread.h>
#include <unistd.h>

#include <windows.h>
#include <psapi.h>
#include <memoryapi.h>
#include <heapapi.h>

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

static uint32_t (__attribute__((stdcall)) *send_constant_force_orig)(void *ctx, float param_1, float param_2);
uint32_t __attribute__((stdcall))send_constant_force_patched(void *ctx, float param_1, float param_2){
	#if 1
	LOG("%s: 0x%08x %f %f\n", __func__, ctx, param_1, param_2);
	#endif
	param_1 = param_1 * 10;
	return send_constant_force_orig(ctx, param_1, param_2);
}

void hook_send_constant_force(void* begin){
	static bool once = 0;
	if(once){
		return;
	}
	once = true;

	const uint32_t target_offset = (uint32_t)begin + 0x1987C0;

	static uint8_t trampoline[] = {
		// original instruction
		0x00,
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		// mov eax, val
		0xb8, 0x00, 0x00, 0x00, 0x00,
		// jmp eax
		0xff, 0xe0
	};
	memcpy(trampoline, (void *)target_offset, 9);
	*(uint32_t *)&trampoline[10] = target_offset + 9;

	send_constant_force_orig = (uint32_t (__attribute__((stdcall)) *)(void *ctx, float param_1, float param_2))trampoline;
	DWORD old_protect;
	int ret = VirtualProtect(trampoline, sizeof(trampoline), PAGE_EXECUTE_READWRITE, &old_protect);
	if(ret == 0){
		LOG("%s: failed setting virtual protect for trampoline, %x\n", __func__, GetLastError());
		return;
	}

	uint8_t patch_buf[] = {
		// mov eax, val
		0xb8, 0x00, 0x00, 0x00, 0x00,

		// jmp eax
		0xff, 0xe0
	};
	*(uint32_t *)&patch_buf[1] = (uint32_t)send_constant_force_patched;

	patch(target_offset, patch_buf, sizeof(patch_buf));
	LOG("%s: patched 0x%08x\n", __func__, target_offset);
}

static void patch(void *begin, size_t size){
	#if 1
	// remove limiter
	uint8_t target[] = {0xd9, 0x45, 0xc4, 0xd9, 0xc9, 0xdf, 0xf1, 0xdd, 0xd8, 0x76, 0x07};
	uint8_t patch_buf[] = {0xd9, 0x45, 0xc4, 0xd9, 0xc9, 0xdf, 0xf1, 0xdd, 0xd8, 0xeb, 0x07};
	find_and_patch(begin, size, target, patch_buf, sizeof(target), 0, 1);
	#endif

	// hook
	hook_send_constant_force(begin);
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
				//patch((void *)0x0BE10000, info.SizeOfImage);
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
