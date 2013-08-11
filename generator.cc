
#include "generator.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
	count = 0;
}

Expr::Expr(Op op, Expr* e1, Expr* e2, Expr* e3)
{
    this->op = op;
    opnd[0] = e1;
    opnd[1] = e2;
    opnd[2] = e3;
    count = 0;
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

int Expr::arity()
{
    switch (op) {
    case C0:    return 0;
    case C1:    return 0;
    case VAR:   return 0;

    case NOT:   return 1;
    case SHL1:  return 1;
    case SHR1:  return 1;
    case SHR4:  return 1;
    case SHR16: return 1;

    case PLUS: return 2;
    case AND:  return 2;
    case OR:   return 2;
    case XOR:  return 2;

    case IF0:  return 3;
    case FOLD: return 3;

    default:
        fprintf(stderr, "Error: Unknown op %d\n", op);
        ASSERT(0); 
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

Generator::Generator()
{
	mode_tfold_ = false;
	mode_bonus_ = false;
}

void Generator::allow_all()
{
	for (int op = FIRST_OP; op < MAX_OP; op++)
		add_allowed_op((Op)op);
}

void Generator::generate(int size)
{
	ASSERT(size > 1);
	allow_fold = true;
	allowed_ops_.add(C0);
	allowed_ops_.add(C1);
	allowed_ops_.add(VAR);
	count_ = 0;
    printf("allowed ops=%x\n", allowed_ops_.set_);
	for (int sz = 3; sz <= size; sz++) {
	    done = false;
		left_ = sz - 1;
		cur_size_ = sz;
		ptr_ = 0;
		next_opnd_ = 1;
		arg_ptr_ = 0;
		dummy_root.op = DUMMY_OP;
		push_arg(&dummy_root);
		gen(left_, 1);
	}
	printf("Processes %d items\n", count_);
}

void Generator::push_arg(Expr* owner)
{
//	printf("push_arg %d\n", arg_ptr_+1);
    args_[arg_ptr_++] = owner;
}

Expr* Generator::pop_arg()
{
//	printf("pop_arg %d\n", arg_ptr_ - 1);
	ASSERT(arg_ptr_ > 0);
	return args_[--arg_ptr_];
}

Expr* Generator::peep_arg()
{
	ASSERT(arg_ptr_ > 0);
	return args_[arg_ptr_ - 1];
}

void Generator::push_op(Op op, int var)
{
    if (!allowed_ops_.has(op) && op != FOLD) { // FOLD processed in gen
    	return;
    }

	int my_ptr = ptr_;
	int my_opnds = next_opnd_;
	Expr& e = arena[my_ptr];
	e.op = op;

    Expr* owner = pop_arg();
    if (my_ptr == 0)
    	e.scoped = false;
    else
    	e.scoped = owner->scoped || (owner->op == FOLD && owner->count == 2);
    owner->opnd[owner->count++] = &e;
    e.parent = owner;

	int opnds = e.arity();
	for (int i = 0; i < opnds; i++) {
//		scoped[next_opnd_] = scoped[ptr_];
		push_arg(&e);
	}
/*
		parent[next_opnd_] = &arena[ptr_];
		e.opnd[i] = &arena[next_opnd_];
		next_opnd_++;
	}
*/
	if (op == VAR)
		e.id = var;
//	if (op == FOLD)
//		scoped[my_opnds + 2] = true;
	if (op == FOLD)
		--left_; // for lambda

	--left_;
	++ptr_;

}

void Generator::pop_op()
{
    Expr& e = arena[--ptr_];
    int arity = e.arity();
    for (int i = 0; i < arity; i++)
        pop_arg();
    push_arg(e.parent);
    e.parent->count--;
	next_opnd_ -= e.arity();
	if (e.op == FOLD)
		++left_; // for lambda
	++left_;
}

void Generator::emit(Op op, int var)
{
//	printf("+++emit: %d\n", op);
	if (done)
		return;

    int my_ptr = ptr_;
    push_op(op, var);

/*
	bool is_tfold = false;
	if (my_ptr == 0 && mode_tfold_) {
		ASSERT(op == FOLD);
		push_op(VAR, 0);
		push_op(C0);
//		arena[ptr_] = DUMMY_OP;
		left--;
		is_tfold = true;
	}
*/

	gen(left_, arg_ptr_);
/*
	if (is_tfold) {
		left++;
		pop_op();
		pop_op();
	}
*/
    pop_op();
//	printf("---emit: %d\n", op);
}

void Generator::gen(int left, int free_args)
{
//	printf("gen: %d %d\n", left, free_args);
	if (free_args == 0) {
		// no more free args. closed.
		built();
		return;
	}

    if (ptr_ == 0 && mode_tfold_) {
    	if (left < 5)
    		return;
        push_op(FOLD);
        push_op(VAR, 0);
        push_op(C0);
    	allowed_ops_.del(FOLD);
    	gen(left - 4, free_args);
    	allowed_ops_.add(FOLD);
    	pop_op();
    	pop_op();
    	pop_op();
    	return;
    }

    if (ptr_ == 0 && mode_bonus_) {
    	if (left < 6)
    		return;
    	push_op(IF0);
    	push_op(AND);
    	push_op(C1);
    	gen(left - 3, free_args + 2);
    	pop_op();
    	pop_op();
    	pop_op();
    	return;
    }

    Expr* parent = peep_arg();
    Op parent_op = parent->op;

	if (left >= 4) {
		emit(IF0);
	}

	if (left >= 5) {
		if (allowed_ops_.has(FOLD)) {
			allowed_ops_.del(FOLD);
//			--left_;
		    emit(FOLD);
//		    ++left_;
			allowed_ops_.add(FOLD);
	    }
	}

    // can't emit 0-arity op unless it's the last op
	if (free_args > 1 || left == 1) {
	//	ASSERT(left != 1 || free_args == 1);
		if (parent_op != PLUS &&
			parent_op != XOR &&
			parent_op != OR &&
			parent_op != SHL1 &&
			parent_op != SHR1 &&
			parent_op != SHR4 &&
			parent_op != SHR16 &&
			(parent_op != IF0 || parent->count > 0)) {
		    emit(C0);
	    }
		if (parent_op != SHR1 &&
			parent_op != SHR4 &&
			parent_op != SHR16 &&
			(parent_op != IF0 || parent->count > 0)) {
		    emit(C1);
	    }

    	bool scoped = parent->scoped || (parent_op == FOLD && parent->count == 2);

		int vars = scoped ? 3 : 1;
		for (int i = 0; i < vars; ++i)
		    emit(VAR, i);
	}

	if (left >= 2) {
		if (parent_op != NOT)
		    emit(NOT, 1);
		emit(SHL1);
		emit(SHR1);
		emit(SHR4);
		emit(SHR16);
	}

	if (left > 2) {
		emit(PLUS);
		emit(AND);
		emit(OR);
		emit(XOR);
	}

}

void Generator::built()
{
	Expr* root = &arena[0];
	int size = cur_size_;
	count_++;
	if ((count_ & 0x3fffff) == 0) printf("%9d: [%d] %s\n", count_, size, root->program().c_str());
	done = callback_ ? !callback_->action(root, size) : false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

void Verifier::add(Val input, Val output)
{
	pairs.push_back(std::pair<Val,Val>(input, output));
}

bool Verifier::action(Expr* program, int size)
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
bool Printer::action(Expr* e, int size)
{
    printf("%6d: [%d] %s\n", ++cnt, size, e->program().c_str());
    cnt++;
    return true;
}

#ifdef GEN_MAIN

int main(int argc, char* argv[])
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

    int size = 4;
    if (argc >= 2) size = atoi(argv[1]);

    printf("\nTesting Generator\n");
    Generator g0;
    Printer p;
    g0.add_allowed_op(PLUS);
    g0.add_allowed_op(NOT);
    g0.allow_all();
//    g0.mode_bonus_ = true;
//    g0.mode_tfold_ = true;
//    g0.set_callback(&p);
    g0.generate(size);

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
