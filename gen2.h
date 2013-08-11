#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sstream>
#include <string>
#include <list>
#include <utility>

#define ASSERT assert
using std::string;

typedef uint64_t Val;

enum {
    NO_TOP_SHL1  = 0x01,
    NO_TOP_SHR1  = 0x02,
    NO_TOP_SHR4  = 0x04,
    NO_TOP_SHR16 = 0x08
};

class Context
{
public:
	Context(): count(0) {}

	void push(Val val) { values[count++] = val; }
	void pop() { ASSERT(count); --count; }

	Val get(int id) { return values[id]; }

    int count;
    Val values[1000];
};

enum Op {
	DUMMY_OP, // 0
	FIRST_OP,
	IF0 = FIRST_OP, // 1
	FOLD,     // 2
	NOT,
	SHL1,     // 4
	SHR1,
	SHR4,     // 6
	SHR16,
	AND,      // 8
	OR,
	XOR,      // 10
	PLUS,
	C0,       // 12
	C1,       // 13
	VAR,      // 14
	TFOLD,
	MAX_OP
};

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class OpSet
{
public:
	OpSet() : set_(0) {}

	void add(Op op) { set_ |= 1 << op; }
	bool has(Op op) { return set_ & (1 << op); }
	void del(Op op) { set_ &= ~(1 << op); }

	int set_;
};

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class Expr
{
public:
	enum Flags {
		F_CONST   = 0x1,
		F_IN_FOLD = 0x2
    };

    int arity();
    static int arity(Op op);
    string code();
    string program();
    bool is_var(int id) { return op == VAR && var == id; }
    bool is_const() { return flags & F_CONST; }
    bool is_const(Val x) { return is_const() && val == x; }
    Val run(Val input);
    Val eval(Context* ctx);
    Val do_fold(Context* ctx);

	Op    op;
	Expr* parent;
	Expr* opnd[3];
	int   flags;
	union {
	    Val   val; // for const
	    int   var; // if op is VAR
	};
};

class Callback
{
public:
	virtual ~Callback() {}

	virtual bool action(Expr* e, int size) {};
};

class Arena : public Callback
{
public:
	Arena();

    void set_callback(Callback* c) { callback_ = c; }
    void set_properties(int p) { properties_ = p; }
    void generate(int size, int valence = 1, int args = 1);
	void gen(int left_ops, int valence);

    void try_emit(Op op, int left_ops, int valence);
	void emit(Op op, int var = -1);
	void emit_fold();
	bool action(Expr* e, int size);

	int push_op(Op op, int var = -1);
	void pop_op();

    Expr* peep_arg(int arg);

    void allow_all();
    void add_allowed_op(Op op);
   
    virtual bool complete(Expr* e, int size);

    Callback* callback_;
    Expr* fold_lambda_;

    int size_;
    int num_vars_;
    int count_;
    int args_;
    int bound_args_;
    bool optimize_;
    bool no_more_fold_;
    int valence_;
    bool done_;
    int properties_;

    OpSet allowed_ops_;

    int valents[30];
    int valents_ptr;

    Expr arena[1000];
    int arena_ptr;
};

class ArenaBonus : public Arena
{
public:
	void generate(int size, int args = 1);

    virtual bool complete(Expr* e, int size);
};

class ArenaTfold : public Arena
{
public:
	void generate(int size, int args = 1);

    virtual bool complete(Expr* e, int size);
};

class Printer : public Callback
{
public:
	Printer() : count_(0) {}
	bool action(Expr* e, int size);

	int count_;
};

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class Verifier: public Callback
{
public:
	Verifier() : count(0) {}
	void add(Val input, Val output);

	virtual bool action(Expr* e, int size);

protected:
	typedef std::list< std::pair<Val, Val> > Pairs;

	Pairs pairs;
	int count;
};

class Generator
{
public:
	Generator() : callback_(NULL), mode_bonus_(false), mode_tfold_(false), properties_(0) {}
	void set_callback(Callback* c) { callback_ = c; }
	void generate(int size);

    void set_properties(int p) { properties_ = p; }
    void add_allowed_op(Op op) { allowed_ops_.add(op); }

    bool mode_bonus_;
    bool mode_tfold_;

    OpSet allowed_ops_;
    int properties_;

    Callback* callback_;
};
