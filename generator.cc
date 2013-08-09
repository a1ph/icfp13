#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sstream>
#include <string>
#include <list>
#include <utility>

using std::string;

#define ASSERT assert

typedef uint64_t Val;

string itos(int i) // convert int to string
{
    std::stringstream s;
    s << i;
    return s.str();
}

enum Op {
	ERROR_OP, // 0
	IF0,      // 1
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
	VAR       // 14
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

class Expr
{
public:
    Expr();
	Expr(Op op, Expr* e1 = NULL, Expr* e2 = NULL, Expr* e3 = NULL);
	Expr(Op op, int var_id);
    ~Expr();

    Val run(Val input);
	Val eval(Context* ctx);

    string program();

private:
	friend class Generator;

	string code();
    Val do_fold(Context* ctx);

    Op    op;
    int   id; // for VAR
	int   count;
	Expr* opnd[3];
};

Expr::Expr()
{
	op = ERROR_OP;
	opnd[0] = opnd[1] = opnd[2] = NULL;
}

Expr::Expr(Op op, Expr* e1, Expr* e2, Expr* e3)
{
    this->op = op;
    opnd[0] = e1;
    opnd[1] = e2;
    opnd[2] = e3;
    count = e3 ? 3 : e2 ? 2 : e1 ? 1 : 0;
}

Expr::Expr(Op op, int var_id)
{
	ASSERT(op == VAR);
	this->op = VAR;
	opnd[0] = opnd[1] = opnd[2] = NULL;
	id = var_id;
	count = 0;
}

Expr::~Expr()
{
/*	delete opnd[0];
	delete opnd[1];
    delete opnd[2];
*/
}

string Expr::program()
{
	return "(lambda (x0) " + code() + ")";
}

Val Expr::run(Val input)
{
	Context ctx;
	ctx.push(input);
	return eval(&ctx);
}

Val Expr::eval(Context* ctx)
{
    switch (op) {
    case C0:    return 0;
    case C1:    return 1;
    case VAR:   return ctx->get(id);

    case NOT:   return ~opnd[0]->eval(ctx);
    case SHL1:  return opnd[0]->eval(ctx) << 1;
    case SHR1:  return opnd[0]->eval(ctx) >> 1;
    case SHR4:  return opnd[0]->eval(ctx) >> 4;
    case SHR16: return opnd[0]->eval(ctx) >> 16;

    case PLUS: return opnd[0]->eval(ctx) + opnd[1]->eval(ctx);
    case AND:  return opnd[0]->eval(ctx) & opnd[1]->eval(ctx);
    case OR:   return opnd[0]->eval(ctx) | opnd[1]->eval(ctx);
    case XOR:  return opnd[0]->eval(ctx) ^ opnd[1]->eval(ctx);

    case IF0:  return opnd[0]->eval(ctx) == 0 ? opnd[1]->eval(ctx) : opnd[2]->eval(ctx);
    case FOLD: return do_fold(ctx);

    default:
        fprintf(stderr, "Error: Unknown op %d\n", op);
        ASSERT(0); 
    }
}

Val Expr::do_fold(Context* ctx)
{
	Val data = opnd[0]->eval(ctx);
	Val acc = opnd[1]->eval(ctx);
	for (int i = 0; i < 8; i++) {
		Val byte = data & 0xff;
		ctx->push(acc);
		ctx->push(byte);
		acc = opnd[2]->eval(ctx);
		ctx->pop();
		ctx->pop();
		data >>= 8;
	}
	return acc;
}

string Expr::code()
{
    switch (op) {
    case C0:    return "0";
    case C1:    return "1";
    case VAR:   return "x" + itos(id);

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

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class Callback
{
public:
	virtual ~Callback() {}
	virtual bool action(Expr* e) = 0;
};

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class Generator
{
public:
    void generate(int size, Callback* callback = NULL);

private:
	void gen();
    void emit(Expr e, int opnds);
	void built();

	void push(Expr** pe);
	Expr** pop();
	void next_subtree();

    Expr* program;

	Expr arena[50];
	int ptr;
	int next_opnd;
	int left;
	int vars;

	Expr** stack[50];
	int stack_top;

	int count;

	Callback* callback_;
};

void Generator::generate(int size, Callback* callback)
{
	ASSERT(size > 1);

    callback_ = callback;
	left = size - 1;
	ptr = 0;
	vars = 1;
	next_opnd = 1;
	gen();
}

void Generator::emit(Expr e, int opnds)
{
//	printf("alph: emit %d at %d\n", e.op, ptr);
	--left;
	int save_next_opnd = next_opnd;
	for (int i = 0; i < opnds; i++)
		e.opnd[i] = &arena[next_opnd++];
	arena[ptr++] = e;
	gen();
	next_opnd = save_next_opnd;
	ptr--;
	++left;
}

void Generator::gen()
{
//	printf("gen left=%d ptr=%d\n", left, ptr);
	if (next_opnd == ptr) {
		built();
		return;
	}

	if (left > 0) {
		emit(Expr(C0), 0);
		emit(Expr(C1), 0);
		for (int i = 0; i < vars; ++i)
		    emit(Expr(VAR, i), 0);
	}

	if (left > 1) {
		emit(Expr(NOT), 1);
		emit(Expr(SHL1), 1);
		emit(Expr(SHR1), 1);
		emit(Expr(SHR4), 1);
		emit(Expr(SHR16), 1);
	}

	if (left > 2) {
		emit(Expr(PLUS), 2);
		emit(Expr(AND), 2);
		emit(Expr(OR), 2);
		emit(Expr(XOR), 2);
	}

	if (left > 3) {
		emit(Expr(IF0), 3);
		vars += 2;
		emit(Expr(FOLD), 3);
		vars -= 2;
	}
}

void Generator::built()
{
	callback_->action(&arena[0]);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class Verifier: public Callback
{
public:
	Verifier() : count(0) {}
	void addPair(Val input, Val output);

	virtual bool action(Expr* program);

private:
	typedef std::list< std::pair<Val, Val> > Pairs;

	Pairs pairs;
	int count;
};

void Verifier::addPair(Val input, Val output)
{
	pairs.push_back(std::pair<Val,Val>(input, output));
}

bool Verifier::action(Expr* program)
{
	for (Pairs::iterator it = pairs.begin(); it != pairs.end(); ++it) {
		if (program->run((*it).first) != (*it).second) {
			return true;
		}
    }
    printf("%6d: %s\n", ++count, program->program().c_str());
    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class Printer: public Callback
{
public:
	Printer() : count(0) {}
    virtual bool action(Expr* e);

private:
	int count;
};

bool Printer::action(Expr* e)
{
    printf("%6d: %s\n", ++count, e->program().c_str());
}

int main()
{
    printf("Hello alph!\n");
    Context ctx;

    ctx.push(0x1122334455667788);
    Expr* e = new Expr(SHR16, new Expr(NOT, new Expr(VAR, 0)));
    printf("%s\n", e->program().c_str());
    printf("0x%016lx\n", e->eval(&ctx));
    delete e;
    ctx.pop();

    ctx.push(0x1122334455667788);
    // P = (lambda (x) (fold x 0 (lambda (y z) (or y z))))
    e = new Expr(FOLD, new Expr(VAR, 0), new Expr(C0), new Expr(PLUS, new Expr(VAR, 1), new Expr(VAR, 2)));
    printf("%s\n", e->program().c_str());
    printf("0x%016lx\n", e->eval(&ctx));
    delete e;
    ctx.pop();

    printf("\nTesting Generator\n");
    Generator g;
    Printer p;
    g.generate(4, &p);

    printf("\nTesting Verifier\n");
    Verifier v;
    v.addPair(0x1122334455667788, 0x0000EEDDCCBBAA99);
    v.addPair(5, 0x0000FFFFFFFFFFFF);
    v.addPair(6, 0x0000FFFFFFFFFFFF);
    v.addPair(7, 0x0000FFFFFFFFFFFF);
    g.generate(6, &v);

	return 0;
}