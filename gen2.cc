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

class Expr
{
public:
	enum Flags {
		F_CONST   = 0x1,
		F_IN_FOLD = 0x2
    };

    int arity();
    string code();
    string program();
    bool is_var(int id) { return op == VAR && var == id; }
    bool is_const() { return flags & F_CONST; }
    bool is_const(Val x) { return is_const() && val == x; }
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

int Expr::arity()
{
	switch (op)
	{
		case C0:
		case C1:
		case VAR:
		    return 0;

		case NOT:
		case SHL1:
		case SHR1:
		case SHR4:
		case SHR16:
		    return 1;

		case PLUS:
		case XOR:
		case OR:
		case AND:
		    return 2;

		case IF0:
		case FOLD:
		    return 3;
        
        default:
            fprintf(stderr, "bad op at arity\n");
            exit(1);
	}
}

Val Expr::eval(Context* ctx)
{
	if (flags & F_CONST)
		return val;

	Val res = 0;
    switch (op) {
    case C0:    res = 0; break;
    case C1:    res = 1; break;
    case VAR:   res = ctx->get(var); break;

    case NOT:   res = ~opnd[0]->eval(ctx); break;
    case SHL1:  res = opnd[0]->eval(ctx) << 1; break;
    case SHR1:  res = opnd[0]->eval(ctx) >> 1; break;
    case SHR4:  res = opnd[0]->eval(ctx) >> 4; break;
    case SHR16: res = opnd[0]->eval(ctx) >> 16; break;

    case PLUS: res = opnd[0]->eval(ctx) + opnd[1]->eval(ctx); break;
    case AND:  res = opnd[0]->eval(ctx) & opnd[1]->eval(ctx); break;
    case OR:   res = opnd[0]->eval(ctx) | opnd[1]->eval(ctx); break;
    case XOR:  res = opnd[0]->eval(ctx) ^ opnd[1]->eval(ctx); break;

    case IF0:  res = opnd[0]->eval(ctx) == 0 ? opnd[1]->eval(ctx) : opnd[2]->eval(ctx); break;
    case FOLD: res = do_fold(ctx); break;

    default:
        fprintf(stderr, "Error: Unknown op %d\n", op);
        ASSERT(0); 
    }
//    printf("eval: %d = 0x%016lx\n", op, res);
    return res;
}

Val Expr::do_fold(Context* ctx)
{
	Val data = opnd[0]->eval(ctx);
	Val acc = opnd[1]->eval(ctx);
	for (int i = 0; i < 8; i++) {
		Val byte = data & 0xff;
		ctx->push(byte);
		ctx->push(acc);
		acc = opnd[2]->eval(ctx);
		ctx->pop();
		ctx->pop();
		data >>= 8;
	}
	return acc;
}

static string itos(int i) // convert int to string
{
    std::stringstream s;
    s << i;
    return s.str();
}

string Expr::code()
{
    switch (op) {
    case C0:    return "0";
    case C1:    return "1";
    case VAR:   return "x" + itos(var);

    case NOT:   return "(not "   + opnd[0]->code() + ")";
    case SHL1:  return "(shl1 "  + opnd[0]->code() + ")";
    case SHR1:  return "(shr1 "  + opnd[0]->code() + ")";
    case SHR4:  return "(shr4 "  + opnd[0]->code() + ")";
    case SHR16: return "(shr16 " + opnd[0]->code() + ")";

    case PLUS: return "(plus " + opnd[0]->code() + " " + opnd[1]->code() + ")";
    case AND:  return "(and "  + opnd[0]->code() + " " + opnd[1]->code() + ")";
    case OR:   return "(or "   + opnd[0]->code() + " " + opnd[1]->code() + ")";
    case XOR:  return "(xor "  + opnd[0]->code() + " " + opnd[1]->code() + ")";

    case IF0:  return "(if0 "  + opnd[0]->code() + " " + opnd[1]->code() + " " + opnd[2]->code() + ")";

    // The code below assumes there's the only fold operation.
    case FOLD: return "(fold " + opnd[0]->code() + " " + opnd[1]->code() +
    	                  " (lambda (x1 x2) " + opnd[2]->code() + "))";

    default:
        fprintf(stderr, "Error: Unknown op %d\n", op);
        ASSERT(0); 
    }
}

string Expr::program()
{
	return "(lambda (x0) " + code() + ")";
}

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
    void generate(int size, int valence = 1, int args = 1);
	void gen(int left_ops, int valence);

	void emit(Op op, int var = -1);
	void emit_fold();
	bool action(Expr* e, int size);

	int push_op(Op op, int var = -1);
	void pop_op();

    Expr* peep_arg(int arg);

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

    int valents[30];
    int valents_ptr;

    Expr arena[1000];
    int arena_ptr;
};

Arena::Arena()
{
//	memset(arena, 0, sizeof(arena));
	optimize_ = true;
	callback_ = NULL;
	no_more_fold_ = false;
}

void Arena::generate(int size, int valence, int args)
{
//	printf("generate %d %d %d\n", size, valence, args);
	count_ = 0;
	optimize_ = true;
	int min_size = valence + 1;
	for (int sz = min_size; sz <= size; sz++) {
		size_ = sz - 1;
//		printf("current size %d\n", size_);
		arena_ptr = 0;
		valents_ptr = 0;
		valence_ = valence;
		num_vars_ = args;
	    gen(size_, 0);
	}
//	printf("generated: %d\n", count_);
}

void Arena::gen(int left_ops, int valence)
{
//	printf("gen %d %d\n", left_ops, valence);
	int max_valence = valence_ + (left_ops - 1) * 2;
	int min_valence = valence_ - (left_ops - 1);

	if (min_valence <= valence + 1 && valence + 1 <= max_valence) {
		emit(C0);
		emit(C1);
		for (int i = 0; i < num_vars_; i++)
			emit(VAR, i);
	}

	if (min_valence <= valence + 0 && valence + 0 <= max_valence && valence >= 1) {
    	Expr* opnd = peep_arg(0);
    	if (!optimize_ || opnd->op != NOT) {
    	    emit(NOT);
        }
        // Do not shift 0
    	if (!optimize_ || !opnd->is_const(0)) {
	    	emit(SHL1);
	    	emit(SHR1);
	    	emit(SHR4);
	    	emit(SHR16);
	    }
    }

	if (min_valence <= valence - 1 && valence - 1 <= max_valence && valence >= 2) {
    	Expr* opnd1 = peep_arg(0);
    	Expr* opnd2 = peep_arg(1);
    	if (!optimize_ || !opnd1->is_const(0) && !opnd2->is_const(0)) {
	    	emit(PLUS);
	    	emit(OR);
	    	emit(XOR);
	    	emit(AND);
	    }
    }

	if (min_valence <= valence - 2 && valence - 2 <= max_valence && valence >= 3) {
    	Expr* cond_opnd = peep_arg(0);
    	if (!optimize_ || !(cond_opnd->flags & Expr::F_CONST))
    	    emit(IF0);
    }

    // fold consumes at least 3 ops: fold, lambda, and its expr.
	max_valence = valence_ + (left_ops - 3) * 2;
	min_valence = valence_ - (left_ops - 3);
	if (min_valence <= valence - 1 && valence - 1 <= max_valence && valence >= 2) {
    	emit_fold();
    }
}

Expr* Arena::peep_arg(int arg)
{
	return &arena[valents[valents_ptr - arg - 1]];
}

void Arena::emit(Op op, int var)
{
	int my_ptr = push_op(op, var);

    Expr& e = arena[my_ptr];

//    printf("arena: %d   size:%d\n", arena_ptr, size_);
    if (size_ == arena_ptr) {
    	complete(&e, size_ + 1);
    } else {
	    gen(size_ - arena_ptr, valents_ptr);
	}

    pop_op();
}

bool Arena::complete(Expr* e, int size)
{
	count_++;
	return callback_ ? callback_->action(e, size) : true;
}

int Arena::push_op(Op op, int var)
{
	int my_ptr = arena_ptr++;
//	printf("push_op %d -> [%d]: ", op, my_ptr);
	Expr& e = arena[my_ptr];
	memset(&e, 0, sizeof(Expr));
	e.op = op;
	e.flags = 0;
	e.val = (unsigned long)-1;
	if (op == VAR)
		e.var = var;

    bool const_expr = op != VAR && op != FOLD;
    int arity = e.arity();
    if (op == FOLD)
    	arity = 2; // special processing for the fold's lambda
    for (int i = 0; i < arity; i++) {
    	ASSERT(valents_ptr > 0);
    	int opnd_index = valents[--valents_ptr];
    	Expr& opnd = arena[opnd_index];
    	e.opnd[i] = &opnd;
    	opnd.parent = &e;
    	const_expr = const_expr && (opnd.flags & Expr::F_CONST);
    }
    if (op == FOLD) {
	    e.opnd[2] = fold_lambda_;
	    fold_lambda_->parent = &e;
	}

    if (const_expr) {
    	if (op == FOLD) {
	    	Context ctx;
   		 	e.val = e.eval(&ctx);
	    } else {
	    	// no context as it shouldn't reference any var
	    	// things like (plus x 0) should no appear in the code as well.
   		 	e.val = e.eval(NULL);
	    }
    	// eval before setting the flag, otherwise it won't really do eval.
    	e.flags |= Expr::F_CONST;
    }

	valents[valents_ptr++] = my_ptr;

//    printf("curried into: %s\n", e.code().c_str());
//	printf("\tvalents after push_op %d\n", valents_ptr);
	return my_ptr;
}

void Arena::pop_op()
{
	arena_ptr--;
//	printf("pop_op [%d]\n", arena_ptr);

	int my_ptr = arena_ptr;
	Expr& e = arena[my_ptr];

    valents_ptr--;

    int arity = e.arity();
    if (e.op == FOLD)
    	arity = 2; // special processing for the fold's lambda
	for (int i = arity - 1; i >= 0; i--) {
		valents[valents_ptr++] = e.opnd[i] - arena;
	}
}

void Arena::emit_fold()
{
	if (no_more_fold_)
		return;

	no_more_fold_ = true;
	int max_size = size_ - arena_ptr - 1; // 1 takes FOLD
    Arena fold_lambda;
    fold_lambda.set_callback(this);
    fold_lambda.no_more_fold_ = true; // disable inner folds
    fold_lambda.generate(max_size, 1, 3);
    no_more_fold_ = false;
}

bool Arena::action(Expr* expr, int size)
{
	if (optimize_ && (expr->is_const() || expr->is_var(0)))
		return true;

    arena_ptr += size;
	fold_lambda_ = expr;
	emit(FOLD);
    arena_ptr -= size;
	return true;
}

class BonusArena : public Arena
{
public:
	void generate(int size, int args = 1);

    virtual bool complete(Expr* e, int size);
};

void BonusArena::generate(int size, int args)
{
	no_more_fold_ = true;
    Arena::generate(size - 3, 3, 1);
}

bool BonusArena::complete(Expr* e, int size)
{
    push_op(C1);
    push_op(AND);
    int op_ptr = push_op(IF0);
    bool res = Arena::complete(&arena[op_ptr], size);
    pop_op();
    pop_op();
    pop_op();
    return res;
}

class Printer : public Callback
{
public:
	Printer() : count_(0) {}
	bool action(Expr* e, int size);

	int count_;
};

bool Printer::action(Expr* e, int size)
{
    count_++;
	static int cnt = 0;
	cnt++;
	if ((cnt & 0x3fffff) == 0) printf("%9d: [%2d] %s\n", cnt, size, e->program().c_str());
//	printf("%9d: [%2d] %s\n", cnt, size, e->program().c_str());
	return true;
}

int main()
{
	Printer p;
	Arena a;
	a.set_callback(&p);
	a.generate(11);

	printf("Total: %d\n", p.count_);

    return 0;
}