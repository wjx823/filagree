#include "ui.h"
#include "hal.h"

#define THROW(x) { throw(x) return; }
#define ATTEMPT(x, y) { x; if (context->vm_exception) return y; }

void throw(struct Context *context, const char *msg) {
    context->vm_exception = variable_new_str(context, byte_array_from_string("missing UI element type"));
}


uint32_t get_int(struct array *params)
{
    null_check(params);
    if (params->current >= params->length)
        THROW("missing integer parameter");
        
}

void button(bool vertical, uint32_t x, uint32_t y, struct array *params)
{
    uint32_t w = get_int(params);
    uint32_t h = get_int(params);
    const char *str  = get_str(params);
    const char *img1 = get_str(params);
    const char *img2 = get_str(params);

    hal_button(x, y, w, h, str, img1, img2);
    if (vertical)
        y += h;
    else
        x += w;
}

void layout(struct Context *context, bool vertical, uint32_t x, uint32_t y, struct array *params)
{
    struct variable *kind = (struct variable*)array_get(params, 0);
    if (!kind || kind->type != VAR_STR || !kind->str->length) {
        context->vm_exception = variable_new_str(context, byte_array_from_string("missing UI element type"));
        return;
    }
    params->current++;

    switch (kind->str->data[0]) {
        case 'v': layout(context, true,  x, y, params); break;
        case 'h': layout(context, false, x, y, params); break;
        case 'b': button(vertical, x, y, params);       break;
        case 't': table(vertical, x, y, params);        break;
        case 'i': input(vertical, x, y, params);        break;
        case 'l': label(vertical, x, y, params);        break;
        case 'p': picture(vertical, x, y, params);      break;
        default:
            context->vm_exception = variable_new_str(context, byte_array_from_string("unknown UI element"));
            return;
    }
}

struct array *array_from_stack(struct stack *stack)
{
    null_check(stack);
    struct array *result = array_new();
    while (!stack_empty(stack))
        array_add(result, stack_pop(stack));
    return result;
}

void sys_ui(struct Context *context)
{
    create_app_window();
    
    stack_pop(context->rhs); // self
    if (stack_empty(context->rhs))
        return;
    struct array *params = array_from_stack(context->rhs);
    layout(context, true, 0, 0, params);
}
