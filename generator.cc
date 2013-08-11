
#include "generator.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sstream>
#include <string>
#include <list>
#include <utility>

string itos(int i) // convert int to string
{
    std::stringstream s;
    s << i;
    return s.str();
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

Expr::Expr()
{
	op = DUMMY_OP;
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
	Val res = 0;
    switch (op) {
    case C0:    res = 0; break;
    case C1:    res = 1; break;
    case VAR:   res = ctx->get(id); break;

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

void Generator::allow_all()
{
	for (int op = FIRST_OP; op < MAX_OP; op++)
		add_allowed_op((Op)op);
	allowed_ops_.del(TFOLD);
}

void Generator::generate(int size, Callback* callback)
{
	ASSERT(size > 1);
    done = false;
    callback_ = callback;
	left = size - 1;
	ptr = 0;
	next_opnd = 1;
	scoped[0] = false;
	allow_fold = true;
	if (allowed_ops_.has(TFOLD))
		allowed_ops_.add(FOLD);
	allowed_ops_.add(C0);
	allowed_ops_.add(C1);
	allowed_ops_.add(VAR);
    printf("allowed ops=%x\n", allowed_ops_.set_);
	gen();
}

void Generator::emit(Expr e, int opnds)
{
	if (done)
		return;

    if (!allowed_ops_.has(e.op) && e.op != FOLD) { // FOLD processed in gen
//    	printf("return %d is not allowed\n", e.op);
    	return;
    }

	--left;
	int save_next_opnd = next_opnd;
	for (int i = 0; i < opnds; i++) {
		scoped[next_opnd] = scoped[ptr];
		parents[next_opnd] = &arena[ptr];
		e.opnd[i] = &arena[next_opnd++];
	}
	if (e.op == FOLD)
		scoped[save_next_opnd + 2] = true;
	arena[ptr++] = e;
	bool was_tfold = false;
	if (ptr == 1 && e.op == FOLD && allowed_ops_.has(TFOLD)) {
		arena[save_next_opnd] = Expr(VAR, 0);
		arena[save_next_opnd + 1] = Expr(C0);
		ptr += 2;
		left -= 2;
		was_tfold = true;
	}
	gen();
	if (was_tfold) {
		ptr -= 2;
		left += 2;
	}
	next_opnd = save_next_opnd;
	ptr--;
	++left;
}

void Generator::gen()
{
	if (next_opnd == ptr) {
		built();
		return;
	}

    if (ptr == 0 && left > 3 && allowed_ops_.has(TFOLD)) {
    	allowed_ops_.del(FOLD);
    	emit(Expr(FOLD), 3);
    	return;
    }

    Op parent_op = ptr != 0 ? parents[ptr]->op : DUMMY_OP;

	if (left > 0) {
		if (parent_op != PLUS &&
			parent_op != XOR &&
			parent_op != OR &&
			parent_op != SHL1 &&
			parent_op != SHR1 &&
			parent_op != SHR4 &&
			parent_op != SHR16) {
		    emit(Expr(C0), 0);
	    }
		if (parent_op != SHR1 &&
			parent_op != SHR4 &&
			parent_op != SHR16) {
		    emit(Expr(C1), 0);
	    }
		int vars = scoped[ptr] ? 3 : 1;
		for (int i = 0; i < vars; ++i)
		    emit(Expr(VAR, i), 0);
	}

	if (left > 1) {
	//	if (parent_op != NOT)
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
		if (allowed_ops_.has(FOLD)) {
			allowed_ops_.del(FOLD);
		    emit(Expr(FOLD), 3);
			allowed_ops_.add(FOLD);
	    }
	}
}

void Generator::built()
{
	static int cnt = 0;
	if ((++cnt & 0x3fffff) == 0) printf("%9d: %s\n", cnt, arena[0].program().c_str());
	done = !callback_->action(&arena[0]);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

void Verifier::add(Val input, Val output)
{
	pairs.push_back(std::pair<Val,Val>(input, output));
}

bool Verifier::action(Expr* program)
{
	for (Pairs::iterator it = pairs.begin(); it != pairs.end(); ++it) {
		Val actual = program->run((*it).first);
		if (actual != (*it).second) {
//			printf("%s failed 0x%lx -> 0x%lx\n", program->program().c_str(), (*it).first, actual);
			return true;
		}
    }
    printf("%6d: %s\n", ++count, program->program().c_str());
#if 0
	for (Pairs::iterator it = pairs.begin(); it != pairs.end(); ++it) {
		printf("    0x%016lx -> 0x%016lx\n", (*it).first, (*it).second);
    }
#endif
    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

int cnt = 0;
bool Printer::action(Expr* e)
{
    //printf("%6d: %s\n", ++count, e->program().c_str());
    cnt++;
    return true;
}

#ifdef TEST

int main()
{
    printf("Hello alph!\n");

    Expr* e = new Expr(SHR16, new Expr(NOT, new Expr(VAR, 0)));
    printf("%s\n", e->program().c_str());
    printf("0x%016lx\n", e->run(0x1122334455667788));

    // P = (lambda (x) (fold x 0 (lambda (y z) (or y z))))
    e = new Expr(FOLD, new Expr(VAR, 0), new Expr(C0), new Expr(PLUS, new Expr(VAR, 1), new Expr(VAR, 2)));
    printf("%s\n", e->program().c_str());
    printf("0x%016lx\n", e->run(0x1122334455667788));

#define E_ new Expr

    // (lambda (x0) (shl1 (fold x0 0 (lambda (x1 x2) (plus x2 (shr1 (if0 x1 x2 0)))))))
    e = E_(SHL1, E_(FOLD, E_(VAR, 0), E_(C0), E_(PLUS, E_(VAR, 2), E_(SHR1, E_(IF0, E_(VAR, 1), E_(VAR, 2), E_(C0))))));
    printf("%s\n", e->program().c_str());
    printf("0x%016lx\n", e->run(0x0400000000F88008));

    // (lambda (x0) (fold x0 0 (lambda (x y) (plus x (shl1 (shl1 (shl1 (shl1 y))))))))
    e = E_(FOLD, E_(VAR, 0), E_(C0), E_(PLUS, E_(VAR, 1), E_(SHL1, E_(SHL1, E_(SHL1, E_(SHL1, E_(VAR, 2)))))));
    printf("%s\n", e->program().c_str());
    printf("0x%016lx\n", e->run(0x70605040302));

    printf("\nTesting Generator\n");
    Generator g0;
    Printer p;
    g0.add_allowed_op(PLUS);
    g0.add_allowed_op(NOT);
  //  g0.add_allowed_op(TFOLD);
    g0.allow_all();
    g0.generate(10, &p);
    printf("%d\n", cnt);

#if 0
    printf("\nTesting Verifier\n");
    Generator g;
    Verifier v;
    Val inp[] = { 0xB445FBB8CDDCF9F8, 0xEFE7EA693DD952DE, 0x6D326AEEB275CF14, 0xBB5F96D91F43B9F3, 0xF246BDD3CFDEE59E, 0x28E6839E4B1EEBC1, 0x9273A5C811B2217B, 0xA841129BBAB18B3E };
    Val out[] = { 0x0000000000000010, 0x0000000000000000, 0x0000000000000024, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000 };
    for (int i = 0; i < sizeof(inp) / sizeof(*inp); i++) {
        v.add(inp[i], out[i]);
    }
    g.add_allowed_op(IF0);
//    g.add_allowed_op(OR);
    g.add_allowed_op(AND);
//    g.add_allowed_op(PLUS);
//    g.add_allowed_op(SHL1);
    g.add_allowed_op(SHR1);
//    g.add_allowed_op(SHR16);
//    g.add_allowed_op(SHR4);
    g.add_allowed_op(TFOLD);
//    g.add_allowed_op(FOLD);
//    g.add_allowed_op(XOR);
    g.generate(12, &v);
#endif

	return 0;
}
#endif
