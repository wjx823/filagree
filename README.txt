Filagree is a scripting language. Its design goals are:
- simplicity
- portability
- low footprint (VM is ~20KB on ARM)

It is dynamically typed, garbage collected, has few keywords and minimal syntax, and integrates easily with C.

------- Tutorial

filagree may be built and run from the command line:

    $ make
    $ ./filagree
    f> a = 7+8
    f> sys.print(a)

    15

    f> ^D
    $ 

If given an argument, it will interpret a file:

    $ echo "sys.print('how you ' + 'doin')" > iamafile.fg
    $ ./filagree imafile
  how you doin
    $

There is one structure, a list, which may contain values indexed by number (array) and/or string (map):

    f> a = [3, 1]
    f> b = [4, 'p':5, 'q':a] 
    f> sys.print(b['p'] + '  ' +  b.q[0])

    5  3

Actually, any variable may contain mapped values:

    f> a = 7
    f> a.too = 'buckle my shoe'

Comments are either:

    x = 1 # single line, or
    y = 2 /* multiple
	    line */

Functions are first-order variables:

    f = function(p,q,r)
        return p+q+r, ' boo'    # r is nil
    end
    a, b = 7, 8
    c, d = f(a,b)
    sys.print(c + d)
    # output: 15 boo

    if 0 then
        sys.print('zero')
    else
        sys.print('one')
    end
    # output: one

    if a = false then
        sys.print('seven')
    else if b = 8 then
        sys.print('ate')
    end
    # output: ate

    n = 3
    while n
        sys.print(n)
        n = n-1
    end
    # output:
	# 3
    # 2
    # 1

There are also iterators and comprehensions:

    x = [3,1,4,1,5,9]
    y = [n+1 for n in x where n > 3]    # y = [5,6,10]
    for z in y where z < 9
        sys.print(z)
    end

    # output:
    # 5
    # 6

Exceptions, and try/catch:

    try
        m = n % 2
        if m == 1 then
            throw ['code':99, 'flavor':'strawberry']
        end
    catch e
        sys.print(e.flavor + ' ice cream is yummy')
    end

A few functions come built-in, such as for serialization:

    a = [2,3,4, 'x':7]
    b = a.serialize()    # serializes any variable, including a nested structure
    c = b.deserialize()
    d = c.x + c[2]    # 11

and file access:

    x = [2,'3':4,'5']
    sys.save(x, 'test_file')
    y = sys.load('test_file')
    sys.remove('test_file')
    z = y == x                # true

and sort; you provide the compare function for custom structures:

    p = [3,1,4,1,5,9,6,2]
    p.sort()                # 1,1,2,3,4,5,6,9
    q = [['a':3, 'b':4], ['a':2, 'b':5]]
    q.sort(function(x,y) return x.a - y.a end)
    sys.print(q)            # [['a':2, 'b':5], ['a':3, 'b':4]]

and find / replace:

    p = 'one two three'
    q = p.find('two')         # q = 4
    r = p.part(4,3)           # r = 'two'
    s = p.replace('two', '2') # s = 'one 2 three'

and function arguments:

    f = function(x,y)
        z = sys.args()
        return z.length
    end
    g = f(6,7,8,9)            # g = 4

and atoi:

    n,i = sys.atoi('because 765', 8)  # n = 756, i = 3


Advanced Features

Short circuit:

    f = function()
        throw 99
    end
    g = nil or f()  # throws exception
    h = 7 or f()    # does not throw exception
    i = 8 and f()   # throws exception

Closure:

    x = 7
    f = function(a)(x)
        return a+x
    end 
    g = f(3)        # g = 10

Custom get and set:

    x = ['get': function(self, y)  # custom getter
            if not self!list then  # '!' means don't use custom getter
                self!list = []
            end
            if y == 'p' then
                return 2
            else
                return 3
            end
        end ]
    y = 10*x.p + x.q    # y = 23

    x = ['set': function(self, y, z) # custom setter
            if y == 'p' then
                self!a = z*2    # '!' means don't use custom setter
            else
                self!b = z*4
            end
        end ]
    x.p = 6
    y = 10*x.a + (x.b or 5)    # y = 125

C integration:

    struct variable *my_find(context_p context, const struct byte_array *name) {
        const char *s = byte_array_to_string(s);
        if (!strcmp(s, "x"))
            return variable_new_int(context, 66);
        return NULL;
    }

    struct byte_array *program = build_file('sys.print(x+9) return 12');
    struct variable *r = execute(program, &find);
	// prints '68'; r is a variable with r->type = VM_INT and r->integer = 12


Code Structure

filagree source code consists of eight modules:

- compile: compile fg code into byte code
- interpret: runs either fg code or byte code
- vm: virtual machine
- variable: variable-specific VM code
- sys: built-in functions, such as file and UI access
- serial: serializes and deserializes primitives
- struct: array, byte array, map and stack data structures
- hal: hardware abstraction layer
- util: miscellaneous

The source code includes ports to:
- Android
- iOS
- OSX
- Windows
- Linux

(Actually, it has not been compiled for Windows yet , and there is currently only a HAL implementation for OSX.)

