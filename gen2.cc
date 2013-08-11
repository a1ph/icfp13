#include "gen2.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sstream>
#include <string>
#include <list>
#include <utility>

int Expr::arity(Op op)
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

int Expr::arity()
{
	return arity(op);
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

Val Expr::run(Val input)
{
	Context ctx;
	ctx.push(input);
	return eval(&ctx);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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
	allowed_ops_.add(C0);
	allowed_ops_.add(C1);
	allowed_ops_.add(VAR);
	done_ = false;
	for (int sz = min_size; sz <= size; sz++) {
		size_ = sz - 1;
//		printf("current size %d\n", size_);
		arena_ptr = 0;
		valents_ptr = 0;
		valence_ = valence;
		num_vars_ = args;
	    gen(size_, 0);
	    if (done_)
	    	break;
	}
//	printf("generated: %d\n", count_);
}

void Arena::gen(int left_ops, int valence)
{
//	printf("gen %d %d\n", left_ops, valence);
	int max_valence = valence_ + (left_ops - 1) * 2;
	int min_valence = valence_ - (left_ops - 1);

	if (allowed_ops_.has(IF0) && valence >= 3) {
    	Expr* cond_opnd = peep_arg(0);
    	if (!optimize_ || !(cond_opnd->flags & Expr::F_CONST))
    	    try_emit(IF0, left_ops, valence);
    }

    // fold consumes at least 3 ops: fold, lambda, and its expr.
	int fold_max_valence = valence_ + (left_ops - 3) * 2;
	int fold_min_valence = valence_ - (left_ops - 3);
	if (fold_min_valence <= valence - 1 && valence - 1 <= fold_max_valence && valence >= 2) {
    	emit_fold();
    }

	try_emit(C0, left_ops, valence);
	try_emit(C1, left_ops, valence);
    try_emit(VAR, left_ops, valence);

    if (valence >= 1) {
		Expr* opnd = peep_arg(0);
		if (!optimize_ || opnd->op != NOT) {
		    try_emit(NOT, left_ops, valence);
	    }
	    // Do not shift 0
		if (!optimize_ || !opnd->is_const(0)) {
	    	try_emit(SHL1, left_ops, valence);
	    }
	    if (!optimize_ || !opnd->is_const(0) && !opnd->is_const(1)) {
	    	try_emit(SHR1, left_ops, valence);
	    	try_emit(SHR4, left_ops, valence);
	    	try_emit(SHR16, left_ops, valence);
	    }
	}

	if (valence >= 2) {
		Expr* opnd1 = peep_arg(0);
		Expr* opnd2 = peep_arg(1);
		if (!optimize_ || !opnd1->is_const(0) && !opnd2->is_const(0)) {
	    	try_emit(PLUS, left_ops, valence);
	    	try_emit(OR, left_ops, valence);
	    	try_emit(XOR, left_ops, valence);
	    	try_emit(AND, left_ops, valence);
	    }
	}


}

Expr* Arena::peep_arg(int arg)
{
	return &arena[valents[valents_ptr - arg - 1]];
}

void Arena::try_emit(Op op, int left_ops, int valence)
{
	int max_valence = valence_ + (left_ops - 1) * 2;
	int min_valence = valence_ - (left_ops - 1);

    if (!allowed_ops_.has(op))
    	return;

    if (left_ops == 1) {
    	if (op == SHL1  && (properties_ & NO_TOP_SHL1)) return;
    	if (op == SHR1  && (properties_ & NO_TOP_SHR1)) return;
    	if (op == SHR4  && (properties_ & NO_TOP_SHR4)) return;
    	if (op == SHR16 && (properties_ & NO_TOP_SHR16)) return;
    }
    int arity = Expr::arity(op);

    // ensure we don't miss a fold if it's required
    if (!no_more_fold_ && allowed_ops_.has(FOLD)) {
    	int new_valence = valence - arity + 1;
    	int new_left_ops = left_ops - 1;

        // folds valency of 2
        if (new_valence > 2)
        	new_left_ops -= allowed_ops_.has(IF0) ? (new_valence - 2) / 2 : new_valence - 2; // need to consume extra valence
        else
        	new_left_ops += 2 - new_valence; // need to generate extra valence
    	if (new_left_ops < 3)
    		return; // no chance fold will fit after this op
    }

	if (min_valence <= valence - arity + 1 && valence - arity + 1 <= max_valence && valence >= arity) {
		if (op == VAR) {
			for (int i = 0; i < num_vars_; i++)
				emit(VAR, i);
		} else {
	        emit(op);
	    }
    }
}

void Arena::emit(Op op, int var)
{
	if (done_)
		return;

	int my_ptr = push_op(op, var);

    Expr& e = arena[my_ptr];

//    printf("arena: %d   size:%d\n", arena_ptr, size_);
    if (size_ == arena_ptr) {
    	done_ = complete(&e, size_ + 1);
    } else {
	    gen(size_ - arena_ptr, valents_ptr);
	}

    pop_op();
}

bool Arena::complete(Expr* e, int size)
{
	count_++;
	return callback_ ? !callback_->action(e, size) : false;
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

    if (!allowed_ops_.has(FOLD))
    	return;

	no_more_fold_ = true;
	int max_size = size_ - arena_ptr - 1; // 1 takes FOLD
    Arena fold_lambda;
    fold_lambda.set_callback(this);
    fold_lambda.no_more_fold_ = true; // disable inner folds
    fold_lambda.allowed_ops_ = allowed_ops_;
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

void Arena::add_allowed_op(Op op)
{
	allowed_ops_.add(op);
}

/////////////////////////////////////////////////

void ArenaBonus::generate(int size, int args)
{
	no_more_fold_ = true;
    Arena::generate(size - 3, 3, 1);
}

bool ArenaBonus::complete(Expr* e, int size)
{
    push_op(C1);
    push_op(AND);
    int op_ptr = push_op(IF0);
    bool res = Arena::complete(&arena[op_ptr], size + 3);
    pop_op();
    pop_op();
    pop_op();
    return res;
}

///////////////////////

void ArenaTfold::generate(int size, int args)
{
	no_more_fold_ = true;
    Arena::generate(size - 4, 1, 3);
}

bool ArenaTfold::complete(Expr* e, int size)
{
	fold_lambda_ = e;
    push_op(C0);
    push_op(VAR, 0);
    int op_ptr = push_op(FOLD);
    bool res = Arena::complete(&arena[op_ptr], size + 4);
    pop_op();
    pop_op();
    pop_op();
    return res;
}


bool Printer::action(Expr* e, int size)
{
    count_++;
	static int cnt = 0;
	cnt++;
#ifdef GEN2
	printf("%9d: [%2d] %s\n", cnt, size, e->program().c_str());
#else
	if ((cnt & 0x3fffff) == 0) printf("%9d: [%2d] %s\n", cnt, size, e->program().c_str());
#endif
	return true;
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
    printf("--- %6d: %s\n", ++count, program->program().c_str());
#if 0
	for (Pairs::iterator it = pairs.begin(); it != pairs.end(); ++it) {
		printf("    0x%016lx -> 0x%016lx\n", (*it).first, (*it).second);
    }
#endif
    return false;
}

void Generator::generate(int size)
{
	if (mode_tfold_) {
		ArenaTfold a;
		a.set_callback(callback_);
		a.allowed_ops_ = allowed_ops_;
		a.generate(size);
		printf("count=%d\n", a.count_);
	} else if (mode_bonus_) {
		ArenaBonus a;
		a.set_callback(callback_);
		a.allowed_ops_ = allowed_ops_;
		a.generate(size);
		printf("count=%d\n", a.count_);
	} else {
		Arena a;
		a.set_callback(callback_);
		a.set_properties(properties_);
		a.allowed_ops_ = allowed_ops_;
		a.generate(size);
		printf("count=%d\n", a.count_);
	}
}

#ifdef GEN2

int main()
{
	Printer p;
	Arena a;
	a.set_callback(&p);
	a.add_allowed_op(IF0);
	a.add_allowed_op(FOLD);
	a.generate(GEN2);

	printf("Total: %d\n", p.count_);

    return 0;
}

#endif