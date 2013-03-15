#ifndef NODE_H
#define NODE_H

struct variable *sys_listen(struct context *context);
struct variable *sys_connect(struct context *context);
struct variable *sys_disconnect(struct context *context);

#endif // NODE_H