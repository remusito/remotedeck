#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define rd_mkdir(p) _mkdir(p)
#define RD_SEP '\\'
#else
#include <sys/stat.h>
#define rd_mkdir(p) mkdir(p, 0755)
#define RD_SEP '/'
#endif

static bool config_dir(char *buf, size_t buf_size)
{
#ifdef _WIN32
	const char *base = getenv("APPDATA");
	if(!base)
		return false;
	return snprintf(buf, buf_size, "%s\\remotedeck", base) < (int)buf_size;
#else
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if(xdg && *xdg)
		return snprintf(buf, buf_size, "%s/remotedeck", xdg) < (int)buf_size;
	const char *home = getenv("HOME");
	if(!home)
		return false;
	return snprintf(buf, buf_size, "%s/.config/remotedeck", home) < (int)buf_size;
#endif
}

static bool mkdirs(const char *path)
{
	char tmp[1024];
	if(snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp))
		return false;
	for(char *p = tmp + 1; *p; p++)
	{
		if(*p != '/' && *p != '\\')
			continue;
		char c = *p;
		*p = '\0';
		if(rd_mkdir(tmp) != 0 && errno != EEXIST)
		{
			*p = c;
			continue; // p.ej. "C:" en Windows
		}
		*p = c;
	}
	return rd_mkdir(tmp) == 0 || errno == EEXIST;
}

bool rd_config_path(char *buf, size_t buf_size)
{
	char dir[1024];
	if(!config_dir(dir, sizeof(dir)))
		return false;
	return snprintf(buf, buf_size, "%s%chosts.ini", dir, RD_SEP) < (int)buf_size;
}

void rd_hex_encode(const uint8_t *in, size_t in_size, char *out)
{
	static const char digits[] = "0123456789abcdef";
	for(size_t i = 0; i < in_size; i++)
	{
		out[i * 2] = digits[in[i] >> 4];
		out[i * 2 + 1] = digits[in[i] & 0xf];
	}
	out[in_size * 2] = '\0';
}

static int hex_val(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool rd_hex_decode(const char *in, uint8_t *out, size_t out_size)
{
	if(strlen(in) != out_size * 2)
		return false;
	for(size_t i = 0; i < out_size; i++)
	{
		int hi = hex_val(in[i * 2]), lo = hex_val(in[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return false;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

static void copy_str(char *dst, size_t dst_size, const char *src)
{
	snprintf(dst, dst_size, "%s", src ? src : "");
}

bool rd_config_load(RdConfig *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	char path[1024];
	if(!rd_config_path(path, sizeof(path)))
		return false;
	FILE *f = fopen(path, "r");
	if(!f)
		return true; // aún no hay configuración

	RdHost *cur = NULL;
	char line[1024];
	while(fgets(line, sizeof(line), f))
	{
		line[strcspn(line, "\r\n")] = '\0';
		if(!line[0] || line[0] == '#' || line[0] == ';')
			continue;
		if(strcmp(line, "[host]") == 0)
		{
			cur = cfg->hosts_count < RD_MAX_HOSTS ? &cfg->hosts[cfg->hosts_count++] : NULL;
			continue;
		}
		char *eq = strchr(line, '=');
		if(!eq)
			continue;
		*eq = '\0';
		const char *key = line, *val = eq + 1;

		if(!cur)
		{
			if(strcmp(key, "psn_account_id") == 0)
				copy_str(cfg->psn_account_id, sizeof(cfg->psn_account_id), val);
			continue;
		}
		if(strcmp(key, "nickname") == 0)
			copy_str(cur->nickname, sizeof(cur->nickname), val);
		else if(strcmp(key, "host") == 0)
			copy_str(cur->host, sizeof(cur->host), val);
		else if(strcmp(key, "mac") == 0)
			copy_str(cur->server_mac, sizeof(cur->server_mac), val);
		else if(strcmp(key, "target") == 0)
			cur->target = (ChiakiTarget)atoi(val);
		else if(strcmp(key, "regist_key") == 0)
			rd_hex_decode(val, (uint8_t *)cur->regist_key, sizeof(cur->regist_key));
		else if(strcmp(key, "morning") == 0)
			rd_hex_decode(val, cur->morning, sizeof(cur->morning));
	}
	fclose(f);
	return true;
}

bool rd_config_save(const RdConfig *cfg)
{
	char dir[1024], path[1024];
	if(!config_dir(dir, sizeof(dir)) || !rd_config_path(path, sizeof(path)))
		return false;
	mkdirs(dir);
	FILE *f = fopen(path, "w");
	if(!f)
		return false;

	fprintf(f, "# RemoteDeck - consolas registradas. Contiene claves privadas: no lo compartas.\n");
	if(cfg->psn_account_id[0])
		fprintf(f, "psn_account_id=%s\n", cfg->psn_account_id);
	for(int i = 0; i < cfg->hosts_count; i++)
	{
		const RdHost *h = &cfg->hosts[i];
		char regist_hex[sizeof(h->regist_key) * 2 + 1], morning_hex[sizeof(h->morning) * 2 + 1];
		rd_hex_encode((const uint8_t *)h->regist_key, sizeof(h->regist_key), regist_hex);
		rd_hex_encode(h->morning, sizeof(h->morning), morning_hex);
		fprintf(f, "\n[host]\nnickname=%s\nhost=%s\nmac=%s\ntarget=%d\nregist_key=%s\nmorning=%s\n",
				h->nickname, h->host, h->server_mac, (int)h->target, regist_hex, morning_hex);
	}
	fclose(f);
	return true;
}

static bool host_matches(const RdHost *h, const char *name_or_addr)
{
	return strcmp(h->nickname, name_or_addr) == 0
		|| strcmp(h->host, name_or_addr) == 0
		|| strcmp(h->server_mac, name_or_addr) == 0;
}

RdHost *rd_config_find_host(RdConfig *cfg, const char *name_or_addr)
{
	if(!name_or_addr)
		return cfg->hosts_count > 0 ? &cfg->hosts[0] : NULL;
	for(int i = 0; i < cfg->hosts_count; i++)
		if(host_matches(&cfg->hosts[i], name_or_addr))
			return &cfg->hosts[i];
	return NULL;
}

RdHost *rd_config_upsert_host(RdConfig *cfg, const RdHost *host)
{
	for(int i = 0; i < cfg->hosts_count; i++)
	{
		if(strcmp(cfg->hosts[i].server_mac, host->server_mac) == 0)
		{
			cfg->hosts[i] = *host;
			return &cfg->hosts[i];
		}
	}
	if(cfg->hosts_count >= RD_MAX_HOSTS)
		return NULL;
	cfg->hosts[cfg->hosts_count] = *host;
	return &cfg->hosts[cfg->hosts_count++];
}

bool rd_config_remove_host(RdConfig *cfg, const char *name_or_addr)
{
	for(int i = 0; i < cfg->hosts_count; i++)
	{
		if(!host_matches(&cfg->hosts[i], name_or_addr))
			continue;
		memmove(&cfg->hosts[i], &cfg->hosts[i + 1], sizeof(RdHost) * (size_t)(cfg->hosts_count - i - 1));
		cfg->hosts_count--;
		return true;
	}
	return false;
}
