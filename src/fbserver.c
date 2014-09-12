
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <X11/extensions/XTest.h>

const int PORT = 30002;
const int BUFFERSIZE = 4096;

/* 0 - Quiet
 * 1 - General messages (init, new connections)
 * 2 - 1 + Information on each transfer
 * 3 - 2 + Extra information */
//static int verbose = 0;

static int server_fd = -1;

#define log(level, str, ...) do { \
    if (verbose >= (level)) printf("%s: " str "\n", __func__, ##__VA_ARGS__); \
} while (0)

#define error(str, ...) printf("%s: " str "\n", __func__, ##__VA_ARGS__)

/* Similar to perror, but prints function name as well */
#define syserror(str, ...) printf("%s: " str " (%s)\n", \
                    __func__, ##__VA_ARGS__, strerror(errno))

static Display *dpy;

int init_display() {
    dpy = XOpenDisplay(NULL);

    /* Fixme: check XTest extension available */

    return 0;
}

int write_image(int fd) {
    Window root = DefaultRootWindow(dpy);
    XImage *img = XGetImage(dpy, root, 0, 0, 800, 600, AllPlanes, ZPixmap);
    //printf("size %d %d\n", img->bytes_per_line, img->height);

    int size = img->bytes_per_line * img->height;

    uint32_t* ptr = (uint32_t*)img->data;
    int i;
    for (i = 0; i < size/4; i++) {
        ptr[i] = (ptr[i] & 0x000000ff) << 16 |
                 (ptr[i] & 0x0000ff00) |
                 (ptr[i] & 0x00ff0000) >> 16 |
                 0xff000000;
        //ptr[i] = 0xff0000ff;
    }

    write(fd, img->data, size);

    XDestroyImage(img);

    return 0;
}

int server_init() {
    struct sockaddr_in server_addr;
    int optval;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syserror("Cannot create server socket.");
        exit(1);
    }

    /* SO_REUSEADDR to make sure the server can restart after a crash. */
    optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    /* Listen on loopback interface, port PORT. */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        syserror("Cannot bind server socket.");
        exit(1);
    }

    if (listen(server_fd, 5) < 0) {
        syserror("Cannot listen on server socket.");
        exit(1);
    }

    return 0;
}

int main(int argc, char** argv) {
    init_display();
    server_init();

    int newclient_fd;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);
    unsigned char buffer[BUFFERSIZE];

    while (1) {
        newclient_fd = accept(server_fd,
                          (struct sockaddr*)&client_addr, &client_addr_len);

        if (newclient_fd < 0) {
            syserror("Error accepting new connection.");
            return 1;
        }

        int n;
        while ((n = read(newclient_fd, buffer, 8)) > 0) {
            if (buffer[0] != 'S' && buffer[0] != 'M')
                printf("b %c:%02x%02x%02x%02x%02x%02x%02x\n",
                       buffer[0], buffer[1], buffer[2], buffer[3],
                       buffer[4], buffer[5], buffer[6], buffer[7]);
            switch (buffer[0]) {
            case 'S':
                write_image(newclient_fd);
                break;
            case 'K':
            {
                KeySym ks = ((KeySym)buffer[2]) << 8 | buffer[3];
                KeyCode kc = XKeysymToKeycode(dpy, ks);
                printf("ks=%04x\n", (unsigned int)ks);
                printf("kc=%04x\n", kc);
                if (kc != 0) {
                    XTestFakeKeyEvent(dpy, kc, buffer[1], CurrentTime);
                } else {
                    fprintf(stderr, "Invalid keysym %04x.\n", (unsigned int)ks);
                }
            }
                break;
            case 'C':
            {
                int down = buffer[1];
                int button = buffer[2];
                XTestFakeButtonEvent(dpy, button, down, CurrentTime);
            }
                break;
            case 'M':
            {
                int x = ((KeySym)buffer[1]) << 8 | buffer[2];
                int y = ((KeySym)buffer[3]) << 8 | buffer[4];
                XTestFakeMotionEvent(dpy, 0, x, y, CurrentTime);
            }
                break;
            }
        }
    }

    return 0;
}
