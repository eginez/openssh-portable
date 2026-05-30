/*
 * Smoke test: native AF_UNIX bind / listen / accept / connect / send / recv / close.
 *
 * Verifies the Win32-OpenSSH AF_UNIX-Winsock path in w32fd.c end-to-end without
 * any SSH protocol involvement.
 */

#include "includes.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <direct.h>
#include "../test_helper/test_helper.h"
#include "tests.h"

static const char *test_path = "/tmp/openssh-test-afunix.sock";

void
afunix_tests(void)
{
	int srv, cli, acc;
	struct sockaddr_un addr;
	char buf[16];
	ssize_t n;

	_mkdir("/tmp");
	(void)unlink(test_path);

	{
		TEST_START("AF_UNIX roundtrip");

		srv = socket(AF_UNIX, SOCK_STREAM, 0);
		ASSERT_INT_GE(srv, 0);

		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, test_path, sizeof(addr.sun_path) - 1);

		ASSERT_INT_EQ(bind(srv, (struct sockaddr *)&addr, sizeof(addr)), 0);
		ASSERT_INT_EQ(listen(srv, 4), 0);

		cli = socket(AF_UNIX, SOCK_STREAM, 0);
		ASSERT_INT_GE(cli, 0);
		ASSERT_INT_EQ(connect(cli, (struct sockaddr *)&addr, sizeof(addr)), 0);

		acc = accept(srv, NULL, NULL);
		ASSERT_INT_GE(acc, 0);

		ASSERT_INT_EQ(send(cli, "hello", 5, 0), 5);
		n = recv(acc, buf, sizeof(buf), 0);
		ASSERT_INT_EQ((int)n, 5);
		ASSERT_INT_EQ(memcmp(buf, "hello", 5), 0);

		close(cli);
		close(acc);
		close(srv);
		(void)unlink(test_path);

		TEST_DONE();
	}
}
