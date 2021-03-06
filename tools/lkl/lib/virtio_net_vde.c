#ifdef CONFIG_AUTO_LKL_VIRTIO_NET_VDE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <lkl.h>
#include <lkl_host.h>

#include <libvdeplug.h>

struct lkl_netdev_vde {
	struct lkl_dev_net_ops *ops;
	VDECONN *conn;
};

struct lkl_netdev *nuse_vif_vde_create(char *switch_path);
static int net_vde_tx(struct lkl_netdev *nd, void *data, int len);
static int net_vde_rx(struct lkl_netdev *nd, void *data, int *len);
static int net_vde_poll_with_timeout(struct lkl_netdev *nd, int events,
				     int timeout);
static int net_vde_poll(struct lkl_netdev *nd, int events);

struct lkl_dev_net_ops vde_net_ops = {
	.tx = net_vde_tx,
	.rx = net_vde_rx,
	.poll = net_vde_poll,
};

int net_vde_tx(struct lkl_netdev *nd, void *data, int len)
{
	int ret;
	struct lkl_netdev_vde *nd_vde;

	nd_vde = (struct lkl_netdev_vde *) nd;

	ret = vde_send(nd_vde->conn, data, len, 0);
	if (ret <= 0 && errno == EAGAIN)
		return -1;
	return 0;
}

int net_vde_rx(struct lkl_netdev *nd, void *data, int *len)
{
	int ret;
	struct lkl_netdev_vde *nd_vde;

	nd_vde = (struct lkl_netdev_vde *) nd;

	/*
	 * Due to a bug in libvdeplug we have to first poll to make sure
	 * that there is data available.
	 * The correct solution would be to just use
	 *   ret = vde_recv(nd_vde->conn, data, *len, MSG_DONTWAIT);
	 * This should be changed once libvdeplug is fixed.
	 */
	ret = 0;
	if (net_vde_poll_with_timeout(nd, LKL_DEV_NET_POLL_RX, 0) &
							    LKL_DEV_NET_POLL_RX)
		ret = vde_recv(nd_vde->conn, data, *len, 0);
	if (ret <= 0)
		return -1;
	*len = ret;
	return 0;
}

int net_vde_poll_with_timeout(struct lkl_netdev *nd, int events, int timeout)
{
	int ret;
	struct lkl_netdev_vde *nd_vde;

	nd_vde = (struct lkl_netdev_vde *) nd;

	struct pollfd pollfds[] = {
			{
					.fd = vde_datafd(nd_vde->conn),
			},
			{
					.fd = vde_ctlfd(nd_vde->conn),
					.events = POLLHUP | POLLIN
			}
	};

	if (events & LKL_DEV_NET_POLL_RX)
		pollfds[0].events |= POLLIN;
	if (events & LKL_DEV_NET_POLL_TX)
		pollfds[0].events |= POLLOUT;

	while (poll(pollfds, 2, timeout) < 0 && errno == EINTR)
		;

	ret = 0;

	if (pollfds[1].revents & (POLLHUP | POLLNVAL | POLLIN))
		return -1;
	if (pollfds[0].revents & (POLLHUP | POLLNVAL))
		return -1;

	if (pollfds[0].revents & POLLIN)
		ret |= LKL_DEV_NET_POLL_RX;
	if (pollfds[0].revents & POLLOUT)
		ret |= LKL_DEV_NET_POLL_TX;

	return ret;
}

int net_vde_poll(struct lkl_netdev *nd, int events)
{
	return net_vde_poll_with_timeout(nd, events, -1);
}

struct lkl_netdev *lkl_netdev_vde_create(char const *switch_path)
{
	struct lkl_netdev_vde *nd;
	struct vde_open_args open_args = {.port = 0, .group = 0, .mode = 0700 };
	char *switch_path_copy = 0;

	nd = (struct lkl_netdev_vde *)malloc(sizeof(*nd));
	if (!nd) {
		fprintf(stderr, "Failed to allocate memory.\n");
		/* TODO: propagate the error state, maybe use errno? */
		return 0;
	}
	nd->ops = &vde_net_ops;

	/* vde_open() allows the null pointer as path which means
	 * "VDE default path"
	 */
	if (switch_path != 0) {
		/* vde_open() takes a non-const char * which is a bug in their
		 * function declaration. Even though the implementation does not
		 * modify the string, we shouldn't just cast away the const.
		 */
		size_t switch_path_length = strlen(switch_path);

		switch_path_copy = calloc(switch_path_length + 1, sizeof(char));
		if (!switch_path_copy) {
			fprintf(stderr, "Failed to allocate memory.\n");
			/* TODO: propagate the error state, maybe use errno? */
			return 0;
		}
		strncpy(switch_path_copy, switch_path, switch_path_length);
	}
	nd->conn = vde_open(switch_path_copy, "lkl-virtio-net", &open_args);
	free(switch_path_copy);
	if (nd->conn == 0) {
		fprintf(stderr, "Failed to connect to vde switch.\n");
		/* TODO: propagate the error state, maybe use errno? */
		return 0;
	}

	return (struct lkl_netdev *)nd;
}

#else /* CONFIG_AUTO_LKL_VIRTIO_NET_VDE */

struct lkl_netdev *lkl_netdev_vde_create(char const *switch_path)
{
	fprintf(stderr, "lkl: The host library was compiled without support for VDE networking. Please rebuild with VDE enabled.\n");
	return 0;
}

#endif /* CONFIG_AUTO_LKL_VIRTIO_NET_VDE */
