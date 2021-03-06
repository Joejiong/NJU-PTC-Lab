// #define DEBUG

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "ast.h"
#include "symbol.h"
#include "type.h"
#include "common.h"
#include "object.h"
#include "debug.h"
#include "semantics.h"
#include "optimize.h"

void ir_log(int lineno, char *format, ...);

static list *irs = NULL;

static irvar *ignore_var = NULL;

static list *vars = NULL;

static int var_count = 0;

#pragma region helper functions

static void push_ircode(ircode *code)
{
    irs = list_pushfront(irs, code);
}

static irvar *new_var()
{
    Assert(var_count >= 0, "too many var");
    var_count++;
    irvar *var = new (irvar);
    var->id = var_count;
    sprintf(var->name, "t%d", var_count);
    vars = list_pushfront(vars, var);
    return var;
}

static irlabel *new_named_label(const char *name)
{
    irlabel *l = new (irlabel);
    strcpy(l->name, name);
    return l;
}

static irlabel *new_label()
{
    static int count = 0;

    Assert(count >= 0, "too many label");
    count++;
    irlabel *l = new (irlabel);
    sprintf(l->name, "l%d", count);
    return l;
}

static void gen_label(irlabel *label)
{
    ircode *c = new (ircode);
    c->kind = IR_Label;
    c->label = label;
    push_ircode(c);
}

static void gen_func(irlabel *label)
{
    ircode *c = new (ircode);
    c->kind = IR_Func;
    c->label = label;
    push_ircode(c);
}

static void gen_assign(irop *left, irop *right)
{
    Assert(left->kind == IRO_Variable || left->kind == IRO_Deref, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Assign;
    c->assign.left = left;
    c->assign.right = right;
    push_ircode(c);
}

static void gen_add(irop *target, irop *op1, irop *op2)
{
    Assert(target->kind == IRO_Variable, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Add;
    c->bop.target = target;
    c->bop.op1 = op1;
    c->bop.op2 = op2;
    push_ircode(c);
}

static void gen_sub(irop *target, irop *op1, irop *op2)
{
    Assert(target->kind == IRO_Variable, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Sub;
    c->bop.target = target;
    c->bop.op1 = op1;
    c->bop.op2 = op2;
    push_ircode(c);
}

static void gen_mul(irop *target, irop *op1, irop *op2)
{
    Assert(target->kind == IRO_Variable, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Mul;
    c->bop.target = target;
    c->bop.op1 = op1;
    c->bop.op2 = op2;
    push_ircode(c);
}

static void gen_div(irop *target, irop *op1, irop *op2)
{
    Assert(target->kind == IRO_Variable, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Div;
    c->bop.target = target;
    c->bop.op1 = op1;
    c->bop.op2 = op2;
    push_ircode(c);
}

static void gen_goto(irlabel *label)
{
    ircode *c = new (ircode);
    c->kind = IR_Goto;
    c->label = label;
    push_ircode(c);
}

static void gen_branch(relop_type relop, irop *op1, irop *op2, irlabel *target)
{
    ircode *c = new (ircode);
    c->kind = IR_Branch;
    c->branch.relop = relop;
    c->branch.op1 = op1;
    c->branch.op2 = op2;
    c->branch.target = target;
    push_ircode(c);
}

static void gen_return(irop *ret)
{
    ircode *c = new (ircode);
    c->kind = IR_Return;
    c->ret = ret;
    push_ircode(c);
}

static void gen_dec(irop *op, int size)
{
    Assert(op->kind == IRO_Variable, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Dec;
    c->dec.op = op;
    c->dec.size = size;
    push_ircode(c);
}

static void gen_call(irop *ret, irlabel *label)
{
    Assert(ret->kind == IRO_Variable, "wrong ret type");
    ircode *c = new (ircode);
    c->kind = IR_Call;
    c->call.func = label;
    c->call.ret = ret;
    push_ircode(c);
}

static void gen_arg(irop *arg)
{
    ircode *c = new (ircode);
    c->kind = IR_Arg;
    c->arg = arg;
    push_ircode(c);
}

static void gen_param(irop *param)
{
    Assert(param->kind == IRO_Variable, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Param;
    c->param = param;
    push_ircode(c);
}

static void gen_read(irop *read)
{
    Assert(read->kind == IRO_Variable, "wrong op type");
    ircode *c = new (ircode);
    c->kind = IR_Read;
    c->read = read;
    push_ircode(c);
}

static void gen_write(irop *write)
{
    ircode *c = new (ircode);
    c->kind = IR_Write;
    c->write = write;
    push_ircode(c);
}

#pragma endregion

#pragma region
static void translate_Program(syntax_tree *tree);
static void translate_ExtDefList(syntax_tree *tree);
static void translate_ExtDef(syntax_tree *tree);
static void translate_FunDec(syntax_tree *tree);
static void translate_VarDec(syntax_tree *tree);
static void translate_CompSt(syntax_tree *tree);
static void translate_StmtList(syntax_tree *tree);
static void translate_Stmt(syntax_tree *tree);
static void translate_DefList(syntax_tree *tree);
static void translate_Def(syntax_tree *tree);
static void translate_DecList(syntax_tree *tree);
static void translate_Dec(syntax_tree *tree);
static void translate_Exp(syntax_tree *tree, irvar *target);
static void translate_Cond(syntax_tree *tree, irlabel *true_label, irlabel *false_label);
static list *translate_Args(syntax_tree *tree);
#pragma endregion

static void translate_Program(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "Program");
    // Program : ExtDefList
    //     ;

    AssertEq(tree->type, ST_Program);
    translate_ExtDefList(tree->children[0]);
}
static void translate_ExtDefList(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "ExtDefList");
    // ExtDefList : ExtDef ExtDefList
    //     | /* empty */
    //     ;

    AssertEq(tree->type, ST_ExtDefList);

    if (tree->count == 2)
    {
        translate_ExtDef(tree->children[0]);
        translate_ExtDefList(tree->children[1]);
    }
}
static void translate_ExtDef(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "ExtDef");
    // ExtDef : Specifier ExtDecList SEMI
    //     | Specifier SEMI
    //     | Specifier FunDec CompSt
    //     | Specifier FunDec SEMI
    //     ;
    AssertEq(tree->type, ST_ExtDef);

    switch (tree->children[1]->type)
    {
    case ST_ExtDecList:
    {
        panic("Don't support gloabl var");
    }
    break;
    case ST_FunDec:
    {
        translate_FunDec(tree->children[1]);

        if (tree->children[2]->type == ST_CompSt) // function definition
        {
            translate_CompSt(tree->children[2]);
        }
        else if (tree->children[2]->type == ST_SEMI) // function declare
        {
            panic("Don't support functioin declare");
        }
    }
    break;
    }
}
static void translate_FunDec(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "FunDec");
    // FunDec : ID LP VarList RP
    //     | ID LP RP
    //     ;
    AssertEq(tree->type, ST_FunDec);
    AssertNotNull(tree->sem);
    SES_FunDec *sem = cast(SES_FunDec, tree->sem);
    symbol *sym = st_find(tree->ev->syms, sem->sym->name);
    AssertNotNull(sym);

    irlabel *label = new_named_label(sym->name);
    sym->ir = label;
    gen_func(label);
    int i = 0;
    env *subev = sem->ev;
    while (i < sym->tp->argc)
    {
        char *paramname = sym->tp->args[i]->name;
        symbol *param = st_findonly(subev->syms, paramname);
        AssertNotNull(param);
        irvar *var = new_var();
        if (sym->tp->args[i]->tp->cls == TC_STRUCT || sym->tp->args[i]->tp->cls == TC_ARRAY)
        {
            var->isref = true;
        }
        param->ir = var;

        gen_param(op_var(var));
        i++;
    }
}
static void translate_VarDec(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "VarDec");
    // VarDec : ID
    //     | VarDec LB INT RB
    //     ;
    AssertEq(tree->type, ST_VarDec);

    AssertNotNull(tree->sem);

    SES_VarDec *sem = cast(SES_VarDec, tree->sem);

    irvar *var = new_var();

    switch (sem->sym->tp->cls)
    {
    case TC_STRUCT:
    {
        int sz = type_sizeof(sem->sym->tp);
        irvar *tmp = new_var();
        gen_dec(op_var(tmp), sz);
        gen_assign(op_var(var), op_ref(tmp));
        var->isref = true;
        sem->sym->ir = var;
    }
    break;
    case TC_ARRAY:
    {
        int sz = type_sizeof(sem->sym->tp);
        irvar *tmp = new_var();
        gen_dec(op_var(tmp), sz);
        gen_assign(op_var(var), op_ref(tmp));
        var->isref = true;
        sem->sym->ir = var;
    }
    break;
    case TC_META:
        sem->sym->ir = var;
        break;
    default:
        panic("unexpect dec type %d.", sem->sym->tp->cls);
    }
}
static void translate_CompSt(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "CompSt");
    // CompSt : LC DefList StmtList RC
    //     ;
    AssertEq(tree->type, ST_CompSt);

    translate_DefList(tree->children[1]);

    translate_StmtList(tree->children[2]);
}
static void translate_StmtList(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "StmtList");
    // StmtList : Stmt StmtList
    //     | /* empty */
    //     ;
    AssertEq(tree->type, ST_StmtList);

    if (tree->count > 0)
    {
        translate_Stmt(tree->children[0]);
        translate_StmtList(tree->children[1]);
    }
}
static void translate_Stmt(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "Stmt");
    // Stmt : Exp SEMI
    //     | CompSt
    //     | RETURN Exp SEMI
    //     | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE
    //     | IF LP Exp RP Stmt ELSE Stmt
    //     | WHILE LP Exp RP Stmt
    //     ;
    AssertEq(tree->type, ST_Stmt);
    switch (tree->children[0]->type)
    {
    case ST_Exp: // Exp SEMI
        translate_Exp(tree->children[0], ignore_var);
        break;
    case ST_CompSt: // CompSt
        translate_CompSt(tree->children[0]);
        break;
    case ST_RETURN: // RETURN Exp SEMI
    {
        irvar *var = new_var();
        translate_Exp(tree->children[1], var);
        irvar *var2 = new_var();
        gen_assign(op_var(var2), op_rval(var));
        gen_return(op_rval(var2));
    }
    break;
    case ST_IF:
    {
        if (tree->count == 7) // IF LP Exp RP Stmt ELSE Stmt
        {
            irlabel *lt = new_label(), *lf = new_label(), *le = new_label();
            translate_Cond(tree->children[2], lt, lf);
            gen_label(lt);
            translate_Stmt(tree->children[4]);
            gen_goto(le);
            gen_label(lf);
            translate_Stmt(tree->children[6]);
            gen_label(le);
        }
        else // IF LP Exp RP Stmt
        {
            irlabel *lt = new_label(), *lf = new_label();
            translate_Cond(tree->children[2], lt, lf);
            gen_label(lt);
            translate_Stmt(tree->children[4]);
            gen_label(lf);
        }
    }
    break;
    case ST_WHILE: // WHILE LP Exp RP Stmt
    {
        irlabel *lt = new_label(), *lf = new_label(), *ls = new_label();
        gen_label(ls);
        translate_Cond(tree->children[2], lt, lf);
        gen_label(lt);
        translate_Stmt(tree->children[4]);
        gen_goto(ls);
        gen_label(lf);
    }
    break;
    }
}
static void translate_DefList(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "DefList");
    // DefList : Def DefList
    //     | /* empty */
    //     ;
    AssertEq(tree->type, ST_DefList);

    if (tree->count > 0)
    {
        translate_Def(tree->children[0]);
        translate_DefList(tree->children[1]);
    }
}
static void translate_Def(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "Def");
    // Def : Specifier DecList SEMI
    //     ;
    AssertEq(tree->type, ST_Def);

    translate_DecList(tree->children[1]);
}
static void translate_DecList(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "DecList");
    // DecList : Dec
    //     | Dec COMMA DecList
    //     ;
    AssertEq(tree->type, ST_DecList);

    translate_Dec(tree->children[0]);
    if (tree->count > 1)
        translate_DecList(tree->children[2]);
}
static void translate_Dec(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "Dec");
    // Dec : VarDec
    //     | VarDec ASSIGNOP Exp
    //     ;
    AssertEq(tree->type, ST_Dec);

    translate_VarDec(tree->children[0]);
    if (tree->count > 1)
    {
        SES_VarDec *sem = cast(SES_VarDec, tree->children[0]->sem);
        AssertNotNull(sem->sym->ir);
        irvar *var = cast(irvar, sem->sym->ir);
        irvar *temp = new_var();
        translate_Exp(tree->children[2], temp);
        gen_assign(op_var(var), op_rval(temp));
    }
}
static void translate_Cond(syntax_tree *tree, irlabel *true_label, irlabel *false_label)
{
    ir_log(tree->first_line, "%s", "Exp");
    // Exp : Exp ASSIGNOP Exp
    //     | Exp AND Exp
    //     | Exp OR Exp
    //     | Exp RELOP Exp
    //     | Exp PLUS Exp
    //     | Exp MINUS Exp
    //     | Exp STAR Exp
    //     | Exp DIV Exp
    //     | LP Exp RP
    //     | MINUS Exp %prec NEG
    //     | NOT Exp
    //     | ID LP Args RP
    //     | ID LP RP
    //     | Exp LB Exp RB
    //     | Exp DOT ID
    //     | ID
    //     | INT
    //     | FLOAT
    //     ;
    AssertEq(tree->type, ST_Exp);
    AssertNotNull(tree->sem);
    SES_Exp *sem = cast(SES_Exp, tree->sem);

    switch (tree->count)
    {
    case 2:
    {
        switch (tree->children[0]->type)
        {
        case ST_NOT: // NOT Exp
        {
            translate_Cond(tree->children[1], false_label, true_label);
        }
            return;
        }
    }
    break;
    case 3:
    {
        if (tree->children[0]->type == ST_LP) // LP Exp RP
        {
            translate_Cond(tree->children[1], true_label, false_label);
            return;
        }
        switch (tree->children[1]->type)
        {
        case ST_AND: // Exp AND Exp
        {
            irlabel *sl = new_label();
            translate_Cond(tree->children[0], sl, false_label);
            gen_label(sl);
            translate_Cond(tree->children[2], true_label, false_label);
        }
            return;
        case ST_OR: // Exp OR Exp
        {
            irlabel *sl = new_label();
            translate_Cond(tree->children[0], true_label, sl);
            gen_label(sl);
            translate_Cond(tree->children[2], true_label, false_label);
        }
            return;
        case ST_RELOP:
        {
            irvar *v1 = new_var(), *v2 = new_var();
            translate_Exp(tree->children[0], v1);
            translate_Exp(tree->children[2], v2);

            relop_type rt = *cast(sytd_relop, tree->children[1]->data);
            gen_branch(rt, op_rval(v1), op_rval(v2), true_label);
            gen_goto(false_label);
        }
            return;
        }
    }
    break;
    }
    // default exp
    irvar *v1 = new_var();
    translate_Exp(tree, v1);
    gen_branch(RT_NE, op_rval(v1), op_const(0), true_label);
    gen_goto(false_label);
}
static void gen_arr_copy(irvar *lo, irvar *ro, int sz)
{
    irvar *lt = new_var(), *rt = new_var();
    for (int i = 0; i < sz; i += 4)
    {
        gen_add(op_var(lt), op_var(lo), op_const(i));
        gen_add(op_var(rt), op_var(ro), op_const(i));
        gen_assign(op_deref(lt), op_deref(rt));
    }
}
static void translate_Exp(syntax_tree *tree, irvar *target)
{
    ir_log(tree->first_line, "%s", "Exp");
    // Exp : Exp ASSIGNOP Exp
    //     | Exp AND Exp
    //     | Exp OR Exp
    //     | Exp RELOP Exp
    //     | Exp PLUS Exp
    //     | Exp MINUS Exp
    //     | Exp STAR Exp
    //     | Exp DIV Exp
    //     | LP Exp RP
    //     | MINUS Exp %prec NEG
    //     | NOT Exp
    //     | ID LP Args RP
    //     | ID LP RP
    //     | Exp LB Exp RB
    //     | Exp DOT ID
    //     | ID
    //     | INT
    //     | FLOAT
    //     ;
    AssertEq(tree->type, ST_Exp);
    AssertNotNull(tree->sem);
    SES_Exp *sem = cast(SES_Exp, tree->sem);

    switch (tree->count)
    {
    case 1:
        switch (tree->children[0]->type)
        {
        case ST_INT: // INT
        {
            gen_assign(op_var(target), op_const(*cast(sytd_int, tree->children[0]->data)));
        }
        break;
        case ST_FLOAT: // FLOAT
        {
            panic("No support float");
        }
        break;
        case ST_ID: // ID
        {
            symbol *val = get_symbol_by_id(tree->children[0], tree->ev);
            AssertNotNull(val);
            AssertNotNull(val->ir);
            irvar *var = cast(irvar, val->ir);
            gen_assign(op_var(target), op_var(var));
            target->isref = var->isref;
        }
        break;
        }
        break;
    case 2:
    {
        switch (tree->children[0]->type)
        {
        case ST_MINUS: // MINUS Exp
        {
            irvar *var = new_var();
            translate_Exp(tree->children[1], var);
            gen_sub(op_var(target), op_const(0), op_rval(var));
        }
        break;
        case ST_NOT: // NOT Exp
        {
            // For later
        }
        break;
        default:
            panic("unexpect exp");
            break;
        }
    }
    break;
    case 3:
    {
        switch (tree->children[0]->type)
        {
        case ST_LP: // LP Exp RP
        {
            translate_Exp(tree->children[1], target);
        }
        break;
        case ST_ID: // ID LP RP
        {
            symbol *val = get_symbol_by_id(tree->children[0], tree->ev);
            AssertNotNull(val);
            AssertEq(val->tp->cls, TC_FUNC);
            if (strcmp(val->name, "read") == 0)
            {
                gen_read(op_var(target));
            }
            else
            {
                AssertNotNull(val->ir);
                irlabel *l = cast(irlabel, val->ir);
                gen_call(op_var(target), l);
            }
        }
        break;
        default:
            switch (tree->children[1]->type)
            {
            case ST_DOT: // Exp DOT ID
            {
                irvar *offset = new_var();
                translate_Exp(tree->children[0], offset); // offset must be a address
                AssertEq(offset->isref, true);

                SES_Exp *leftSem = cast(SES_Exp, tree->children[0]->sem);
                int sz = 0;

                {
                    char *name = *cast(sytd_id, tree->children[2]->data);
                    type *a = leftSem->tp;
                    AssertEq(a->cls, TC_STRUCT);
                    for (int i = 0; i < a->memc; i++)
                    {
                        symbol *sym = a->mems[i];
                        if (strcmp(sym->name, name) == 0)
                        {
                            break;
                        }
                        sz += type_sizeof(sym->tp);
                    }
                }
                irvar *t1 = new_var();

                gen_add(op_var(t1), op_var(offset), op_const(sz));
                gen_assign(op_var(target), op_var(t1));
                target->isref = true;
            }
            break;
            case ST_AND: // Exp AND Exp, Exp OR Exp
            case ST_OR:
            case ST_RELOP:
            {
                // For later
            }
            break;
            case ST_ASSIGNOP: // Exp ASSIGNOP Exp
            {
                if (tree->children[0]->count == 1 && tree->children[0]->children[0]->type == ST_ID) // ID = Exp
                {
                    symbol *val = get_symbol_by_id(tree->children[0]->children[0], tree->ev);
                    AssertNotNull(val);
                    AssertNotNull(val->ir);
                    irvar *var = cast(irvar, val->ir);

                    irvar *temp = new_var();

                    translate_Exp(tree->children[2], temp);

                    SES_Exp *leftSem = cast(SES_Exp, tree->children[0]->sem);
                    SES_Exp *rightSem = cast(SES_Exp, tree->children[2]->sem);
                    switch (leftSem->tp->cls)
                    {
                    case TC_META: // INT assign
                    {
                        gen_assign(op_var(var), op_rval(temp));
                        gen_assign(op_var(target), op_var(var));
                    }
                    break;
                    case TC_STRUCT: //Struct assign
                        panic("No struct direct assign support");
                        break;
                    case TC_ARRAY: //Array assign
                    {
                        AssertEq(var->isref, true);
                        AssertEq(temp->isref, true);
                        int sz = type_sizeof(leftSem->tp);
                        int sz2 = type_sizeof(rightSem->tp);
                        gen_arr_copy(var, temp, sz < sz2 ? sz : sz2);
                        gen_assign(op_var(target), op_var(temp));
                        target->isref = true;
                    }
                    break;
                    }
                }
                else if (tree->children[0]->count == 4 && tree->children[0]->children[1]->type == ST_LB) // E[index] = Exp
                {
                    irvar *offset = new_var();
                    irvar *value = new_var();
                    translate_Exp(tree->children[0], offset);
                    AssertEq(offset->isref, true);
                    translate_Exp(tree->children[2], value);
                    SES_Exp *leftSem = cast(SES_Exp, tree->children[0]->sem);
                    SES_Exp *rightSem = cast(SES_Exp, tree->children[2]->sem);
                    switch (leftSem->tp->cls)
                    {
                    case TC_META: // INT assign
                    {
                        gen_assign(op_deref(offset), op_rval(value));
                        gen_assign(op_var(target), op_rval(value));
                    }
                    break;
                    case TC_STRUCT: //Struct assign
                        panic("No struct direct assign support");
                        break;
                    case TC_ARRAY: //Array assign
                    {
                        AssertEq(value->isref, true);
                        int sz = type_sizeof(leftSem->tp);
                        int sz2 = type_sizeof(rightSem->tp);
                        gen_arr_copy(offset, value, sz < sz2 ? sz : sz2);
                        gen_assign(op_var(target), op_var(value));
                        target->isref = true;
                    }
                    break;
                    }
                }
                else if (tree->children[0]->count == 3 && tree->children[0]->children[1]->type == ST_DOT) // E.mem = Exp
                {
                    irvar *offset = new_var();
                    irvar *value = new_var();
                    translate_Exp(tree->children[0], offset);
                    AssertEq(offset->isref, true);
                    translate_Exp(tree->children[2], value);
                    SES_Exp *leftSem = cast(SES_Exp, tree->children[0]->sem);
                    switch (leftSem->tp->cls)
                    {
                    case TC_META: // INT assign
                    {
                        gen_assign(op_deref(offset), op_rval(value));
                        gen_assign(op_var(target), op_rval(value));
                    }
                    break;
                    case TC_STRUCT: //Struct assign
                        TODO();
                        break;
                    case TC_ARRAY: //Array assign
                        TODO();
                        break;
                    }
                }
                else
                {
                    panic("unexpect left val");
                }
            }
            break;
            default: // PLUS, MINUS, ...
            {
                irvar *t1 = new_var(), *t2 = new_var();
                translate_Exp(tree->children[0], t1);
                translate_Exp(tree->children[2], t2);
                switch (tree->children[1]->type)
                {
                case ST_PLUS:
                    gen_add(op_var(target), op_rval(t1), op_rval(t2));
                    break;
                case ST_MINUS:
                    gen_sub(op_var(target), op_rval(t1), op_rval(t2));
                    break;
                case ST_STAR:
                    gen_mul(op_var(target), op_rval(t1), op_rval(t2));
                    break;
                case ST_DIV:
                    gen_div(op_var(target), op_rval(t1), op_rval(t2));
                    break;
                default:
                    panic("unexpect arth type");
                }
            }
            break;
            }
            break;
        }
    }
    break;
    case 4:
    {
        if (tree->children[0]->type == ST_ID) // ID LP Args RP
        {
            symbol *val = get_symbol_by_id(tree->children[0], tree->ev);
            AssertEq(val->tp->cls, TC_FUNC);
            list *params = translate_Args(tree->children[2]);
            if (strcmp(val->name, "write") == 0)
            {
                irvar *p = cast(irvar, params->obj);
                gen_write(op_rval(p));
                gen_assign(op_var(target), op_const(0));
            }
            else
            {
                void **paramArr = list_revto_arr(params);
                for (int i = 0, j = val->tp->argc - 1; i < val->tp->argc && j >= 0; i++, j--)
                {
                    irvar *p = cast(irvar, paramArr[i]);
                    if (val->tp->args[j]->tp->cls == TC_ARRAY || val->tp->args[j]->tp->cls == TC_STRUCT)
                    {
                        AssertEq(p->isref, true);
                        gen_arg(op_var(p));
                    }
                    else
                    {
                        gen_arg(op_rval(p));
                    }
                }
                AssertNotNull(val->ir);
                irlabel *l = cast(irlabel, val->ir);
                gen_call(op_var(target), l);
            }
        }
        else // Exp LB Exp RB
        {
            irvar *offset = new_var();
            translate_Exp(tree->children[0], offset); // offset must be a address
            AssertEq(offset->isref, true);

            SES_Exp *sem = cast(SES_Exp, tree->sem);
            type *baseTp = sem->tp;
            int sz = type_sizeof(baseTp);

            irvar *index = new_var();
            translate_Exp(tree->children[2], index);

            irvar *t1 = new_var(), *t2 = new_var();
            gen_mul(op_var(t1), op_rval(index), op_const(sz));
            gen_add(op_var(t2), op_var(offset), op_var(t1));
            gen_assign(op_var(target), op_var(t2));
            target->isref = true;
        }
    }
    break;
    }
    if (tree->count == 1 && tree->children[0]->type == ST_NOT ||
        tree->count == 3 && (tree->children[1]->type == ST_AND || tree->children[1]->type == ST_OR || tree->children[1]->type == ST_RELOP))
    {
        irlabel *t = new_label(), *f = new_label();
        gen_assign(op_var(target), op_const(0));

        translate_Cond(tree, t, f);

        gen_label(t);
        gen_assign(op_var(target), op_const(1));

        gen_label(f);
    }
}
static list *translate_Args(syntax_tree *tree)
{
    ir_log(tree->first_line, "%s", "Args");
    // Args : Exp COMMA Args
    //     | Exp
    //     ;
    AssertEq(tree->type, ST_Args);

    irvar *var = new_var();
    translate_Exp(tree->children[0], var);
    list *first = new_list();
    first->obj = var;
    if (tree->count > 1)
        first->next = translate_Args(tree->children[2]);
    return first;
}

static bool ir_is_passed = false;
static char ir_buffer[1024];

void ir_error(int type, int lineno, char *format, ...)
{
    ir_is_passed = 0;

    fprintf(stderr, "Error type %d at Line %d: ", type, lineno);

    va_list aptr;
    int ret;

    va_start(aptr, format);
    vsprintf(ir_buffer, format, aptr);
    va_end(aptr);

    fprintf(stderr, "%s.\n", ir_buffer);
}

void ir_log(int lineno, char *format, ...)
{
#ifdef DEBUG

    va_list aptr;
    int ret;

    va_start(aptr, format);
    vsprintf(ir_buffer, format, aptr);
    va_end(aptr);

#endif

    Info("Line %d: %s", lineno, ir_buffer);
}

void ir_prepare()
{
    ir_is_passed = true;
    irs = NULL;
    ignore_var = new_var();
}

ast *ir_translate(syntax_tree *tree)
{
    ast *result = new (ast);

    translate_Program(tree);

    result->len = list_len(irs);
    result->var_count = var_count;
    result->codes = list_revto_arr(irs);
    result->vars = vars;

#ifdef OPTIMIZE
    int count = optimize(result);
#endif

    return result;
}

bool ir_has_passed()
{
    return ir_is_passed;
}

static void printOprand(irop *op, FILE *file)
{
    switch (op->kind)
    {
    case IRO_Variable:
        fprintf(file, "%s", op->var->name);
        break;
    case IRO_Constant:
        fprintf(file, "#%d", op->value);
        break;
    case IRO_Deref:
        fprintf(file, "*%s", op->var->name);
        break;
    case IRO_Ref:
        fprintf(file, "&%s", op->var->name);
        break;
    }
}

void ir_linearise(ast *tree, FILE *file)
{
    ir_log(0, "ir.len: %d", tree->len);
    for (int i = 0; i < tree->len; i++)
    {
        ircode *code = cast(ircode, tree->codes[i]);
        if (code->ignore)
            continue;
        switch (code->kind)
        {
        case IR_Label:
            fprintf(file, "LABEL %s :\n", code->label->name);
            break;
        case IR_Func:
            fprintf(file, "FUNCTION %s :\n", code->label->name);
            break;
        case IR_Assign:
            printOprand(code->assign.left, file);
            fprintf(file, " := ");
            printOprand(code->assign.right, file);
            fprintf(file, "\n");
            break;
        case IR_Add:
            printOprand(code->bop.target, file);
            fprintf(file, " := ");
            printOprand(code->bop.op1, file);
            fprintf(file, " + ");
            printOprand(code->bop.op2, file);
            fprintf(file, "\n");
            break;
        case IR_Sub:
            printOprand(code->bop.target, file);
            fprintf(file, " := ");
            printOprand(code->bop.op1, file);
            fprintf(file, " - ");
            printOprand(code->bop.op2, file);
            fprintf(file, "\n");
            break;
        case IR_Mul:
            printOprand(code->bop.target, file);
            fprintf(file, " := ");
            printOprand(code->bop.op1, file);
            fprintf(file, " * ");
            printOprand(code->bop.op2, file);
            fprintf(file, "\n");
            break;
        case IR_Div:
            printOprand(code->bop.target, file);
            fprintf(file, " := ");
            printOprand(code->bop.op1, file);
            fprintf(file, " / ");
            printOprand(code->bop.op2, file);
            fprintf(file, "\n");
            break;
        case IR_Goto:
            fprintf(file, "GOTO %s\n", code->label->name);
            break;
        case IR_Branch:
            fprintf(file, "IF ");
            printOprand(code->branch.op1, file);
            switch (code->branch.relop)
            {
            case RT_L:
                fprintf(file, " > ");
                break;
            case RT_S:
                fprintf(file, " < ");
                break;
            case RT_LE:
                fprintf(file, " >= ");
                break;
            case RT_SE:
                fprintf(file, " <= ");
                break;
            case RT_E:
                fprintf(file, " == ");
                break;
            case RT_NE:
                fprintf(file, " != ");
                break;
            }
            printOprand(code->branch.op2, file);
            fprintf(file, " GOTO %s", code->branch.target->name);
            fprintf(file, "\n");
            break;
        case IR_Return:
            fprintf(file, "RETURN ");
            printOprand(code->ret, file);
            fprintf(file, "\n");
            break;
        case IR_Dec:
            fprintf(file, "DEC %s %d\n", code->dec.op->var->name, code->dec.size);
            break;
        case IR_Arg:
            fprintf(file, "ARG ");
            printOprand(code->arg, file);
            fprintf(file, "\n");
            break;
        case IR_Call:
            fprintf(file, "%s := CALL %s\n", code->call.ret->var->name, code->call.func->name);
            break;
        case IR_Param:
            fprintf(file, "PARAM %s\n", code->param->var->name);
            break;
        case IR_Read:
            fprintf(file, "READ %s\n", code->read->var->name);
            break;
        case IR_Write:
            fprintf(file, "WRITE ");
            printOprand(code->write, file);
            fprintf(file, "\n");
            break;
        }
    }
}
