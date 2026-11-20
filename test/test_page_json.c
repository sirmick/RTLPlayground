/*
 * test_page_json.c - the JSON generators of httpd/page_impl.c, fed by the
 * table code of rtl837x_port.c over the simulated engine. Both modules are
 * compiled unmodified, so what the web page shows is checked against what
 * the CLI configured, through the same bytes the switch would hold.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_port.h"
#include "machine.h"
#include "page_impl.h"
#include <test/support.h>
#include <test/hw_mock.h>

extern uint8_t  outbuf[TCP_OUTBUF_SIZE];
extern uint16_t slen;
extern uint8_t  vlan_names[VLAN_NAMES_SIZE];
extern uint16_t vlan_ptr;
extern uint16_t management_vlan;

/* The response body: what follows the blank line after the header. */
static const char *body(void)
{
	outbuf[slen < TCP_OUTBUF_SIZE ? slen : TCP_OUTBUF_SIZE - 1] = 0;
	const char *p = strstr((const char *)outbuf, "\r\n\r\n");
	return p ? p + 4 : (const char *)outbuf;
}

static int has(const char *needle) { return strstr(body(), needle) != 0; }

static int count(const char *needle)
{
	int n = 0;
	for (const char *p = body(); (p = strstr(p, needle)); p += strlen(needle))
		n++;
	return n;
}

/* Brackets and braces balance outside strings, quotes pair up. */
static int balanced(void)
{
	int depth = 0, instr = 0;
	for (const char *p = body(); *p; p++) {
		if (*p == '"') { instr = !instr; continue; }
		if (instr) continue;
		if (*p == '{' || *p == '[') depth++;
		if (*p == '}' || *p == ']') depth--;
		if (depth < 0) return 0;
	}
	return depth == 0 && !instr;
}

static void name_vlan(uint16_t vid, const char *name)
{
	static const char h[] = "0123456789abcdef";
	vlan_names[vlan_ptr++] = h[(vid >> 8) & 0xf];
	vlan_names[vlan_ptr++] = h[(vid >> 4) & 0xf];
	vlan_names[vlan_ptr++] = h[vid & 0xf];
	while (*name) vlan_names[vlan_ptr++] = *name++;
	vlan_names[vlan_ptr++] = ' ';
	vlan_names[vlan_ptr] = 0;
}

static void make_vlan(uint16_t vid, uint16_t members, uint16_t tagged)
{
	vlan_settings.vlan = vid;
	vlan_settings.members = members;
	vlan_settings.tagged = tagged;
	vlan_create();
}

static void t_vlan(void)
{
	printf("[test] /vlan.json shows what vlan_create and pvid wrote\n");
	hw_reset(); out_reset(); vlan_ptr = 0; vlan_names[0] = 0;
	make_vlan(10, 0x0015, 0x0004);
	name_vlan(10, "lab");
	port_pvid_set(0, 10);
	port_pvid_set(2, 10);
	port_pvid_set(4, 20);
	send_vlan(10);
	char want[32];
	snprintf(want, sizeof want, "\"untagged_members\":[");
	CHECK(has(want), "untagged_members is the entry word");
	CHECK(has("\"name\":\"lab\""), "name comes from the name table");
	CHECK(has("\"pvid\":[1,3]"), "pvid array has ports 1 and 3, not 5");
	CHECK(balanced(), "valid JSON");
}

static void t_vlanlist(void)
{
	printf("[test] /vlanlist.json lists the VLANs that exist\n");
	hw_reset(); out_reset(); vlan_ptr = 0; vlan_names[0] = 0;
	make_vlan(10, 0x0015, 0x0004);
	make_vlan(20, 0x00f0, 0x0000);
	make_vlan(4094, 0x0001, 0x0000);
	name_vlan(10, "lab");
	name_vlan(4094, "top");
	send_vlanlist();
	CHECK(!has("\"mgmt\""), "no management VLAN: it is mgmt.vlan (P4-1)");
	CHECK(has("{\"id\":10,\"name\":\"lab\"}"), "VLAN 10 with its name");
	CHECK(has("{\"id\":20,\"name\":\"\"}"), "VLAN 20 without a name");
	CHECK(has("{\"id\":4094,\"name\":\"top\"}"), "VLAN 4094 is the last one listed");
	CHECK(count("\"id\":") == 3, "and nothing else");
	CHECK(balanced(), "valid JSON");
	vlan_delete(20);
	send_vlanlist();
	CHECK(count("\"id\":") == 2 && !has("\"id\":20"), "a deleted VLAN drops out");
	management_vlan = 1;
}

int main(void)
{
	printf("== httpd/page_impl.c JSON tests ==\n");
	t_vlan();
	t_vlanlist();
	printf("\n%d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
