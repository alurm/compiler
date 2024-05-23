#include <stddef.h>

int strcmp(const char *, const char *);

void *realloc(void *, size_t);
void free(void *);

int printf(const char *, ...);

#include <assert.h>
#include <stdarg.h>

typedef struct expression expression; struct expression {
	enum {
		is_literal,
		is_addition,
		is_definition,
		is_word,
		is_condition,
	} is;
	union {
		int literal;
		struct addition {
			expression *left;
			expression *right;
		} addition;
		struct definition {
			char *word;
			expression *value;
			expression *context;
		} definition;
		char *word;
		struct condition {
			expression *test;
			expression *yes;
			expression *no;
		} condition;
	};
} programs[] = {
	{
		is_condition,
		.condition = {
			&(expression){ is_literal, 0 },
			&(expression){ is_literal, 2 },
			&(expression){ is_literal, 3 }
		}
	}
,
	{
		is_definition,
		.definition = {
			"five",
			&(expression){ is_literal, 5 },
			&(expression){
				is_addition,
				.addition = {
					&(expression){ is_literal, 4 },
					&(expression){ is_word, .word = "five" },
				}
			}
		}
	}
};


void indent(int by) { for (int i = 0; i < by; i++) printf("\t"); }

void list_expression_recursive(expression, int);

void list_call_expression(char *name, int depth, ...) {
	va_list variadic;
	va_start(variadic, depth);

	_Bool first = 1;

	indent(depth), printf("%s(\n", name);

	while (1) {
		expression *e = va_arg(variadic, expression *);
		if (e == 0) break;

		if (!first) printf(",\n");
		if (first) first = 0;

		list_expression_recursive(*e, depth + 1);
	}

	printf("\n");

	indent(depth), printf(")");
}

#define list_call_expression(name, depth, ...) \
	list_call_expression(name, depth, __VA_ARGS__, 0)

void list_expression_recursive(expression e, int depth) {
	switch (e.is) {
	case is_literal:
		indent(depth), printf("%d", e.literal);
	break;
	case is_addition:
		list_call_expression("add", depth, e.addition.left, e.addition.right);
	break;
	case is_definition:
		list_call_expression(
			"define", depth,
			&(expression){ is_word, .word = e.definition.word },
			e.definition.value, e.definition.context
		);
	break;
	case is_word:
		indent(depth), printf("%s", e.word);
	break;
	case is_condition:
		list_call_expression(
			"if", depth, e.condition.test, e.condition.yes, e.condition.no
		);
	break;
	}
}

void list_expression(expression e) {
	printf("The expression to be run:\n\n");
	list_expression_recursive(e, 1);
	printf("\n");
}

typedef struct environment {
	char *word;
	int value;
	struct environment *rest;
} *environment;

// A tree walking interpreter.
int walk(expression ex, environment en) {
	switch (ex.is) {
	case is_literal: return ex.literal;
	case is_addition: return
		walk(*ex.addition.left, en) + walk(*ex.addition.right, en)
	;
	case is_word:
		while (strcmp(en->word, ex.word) != 0) en = en->rest;
		return en->value;
	case is_definition:
		return walk(
			*ex.definition.context,
			&(struct environment){
				ex.definition.word,
				walk(*ex.definition.value, en),
				en
			}
		);
	case is_condition:
		if (walk(*ex.condition.test, en))
			return walk(*ex.condition.yes, en);
		else
			return walk(*ex.condition.no, en);
	break;
	}
}

typedef struct instruction {
	enum {
		is_load_literal,
		is_add,
		is_copy,
		is_drop,
		is_branch,
		is_jump,
	} is;
	union {
		int load_literal;
		struct {} add;
		int copy;
		int drop;
		int branch;
		int jump;
	};
} instruction;

typedef struct instructions {
	instruction *array;
	int count;
	int limit;
} instructions;

void list_instructions(instructions instructs) {
	printf("The byte code:\n\n");
	for (int i = 0; i < instructs.count; i++) {
		instruction ins = instructs.array[i];
		printf("\t%d", i);
		switch (ins.is) {
		case is_load_literal:
			printf("\tload: %d\n", ins.load_literal);
		break;
		case is_add:
			printf("\tadd\n");
		break;
		case is_copy:
			printf("\tcopy: [%d]\n", ins.copy);
		break;
		case is_drop:
			printf("\tdrop: [%d]\n", ins.drop);
		break;
		case is_branch:
			printf("\tbranch: [%d]\n", ins.branch);
		break;
		case is_jump:
			printf("\tjump: [%d]\n", ins.jump);
		break;
		}
	}
	printf("\n");
}

void push_instruction(instructions *instructs, instruction i) {
	if (instructs->count == instructs->limit) {
		int new_limit = instructs->limit * 2 + 1;
		instructs->array = realloc(instructs->array, new_limit * sizeof (instruction));
		instructs->limit = new_limit;
	}
	instructs->array[instructs->count] = i;
	instructs->count++;
}

instruction pop_instruction(instructions *instructs) {
	assert(instructs->count != 0);
	instructs->count--;
	return instructs->array[instructs->count];
}

typedef struct symbols *symbols;

typedef struct symbols {
	char *word;
	int position;
	struct symbols *rest;
} *symbols;

int compile(expression e, instructions *instructs, symbols s, int stack_size) {
	switch (e.is) {
	case is_literal:
		push_instruction(instructs, (instruction){ is_load_literal, e.literal });
		stack_size++;
	break;
	case is_addition:
		stack_size = compile(*e.addition.left, instructs, s, stack_size);
		stack_size = compile(*e.addition.right, instructs, s, stack_size);
		push_instruction(instructs, (instruction){ is_add });
		stack_size--;
	break;
	case is_definition:
		stack_size = compile(*e.definition.value, instructs, s, stack_size);
		stack_size = compile(
			*e.definition.context,
			instructs,
			&(struct symbols){ e.definition.word, stack_size - 1, s },
			stack_size
		);
		push_instruction(instructs, (instruction){ is_drop, .drop = stack_size - 2 });
		stack_size--;
	break;
	case is_word:
		while (strcmp(s->word, e.word) != 0) s = s->rest;
		push_instruction(instructs, (instruction){ is_copy, .copy = s->position });
		stack_size++;
	break;
	case is_condition: {
		stack_size = compile(*e.condition.test, instructs, s, stack_size);

		// Branch position is not yet known.
		int branch_instruction_position = instructs->count;
		push_instruction(instructs, (instruction){ is_branch });
		stack_size--;

		// Branches should join at the same point later on, right?
		// Stack sizes are most definitely wrong.
		// Have to be careful to use positions and not pointers because of realloc.

		int stack_size_no = compile(*e.condition.no, instructs, s, stack_size);

		// Jump position after "yes" is not yet known.
		int jump_instruction_position = instructs->count;
		push_instruction(instructs, (instruction){ is_jump });

		instruction *branch = instructs->array + branch_instruction_position;
		assert(branch->is == is_branch);
		branch->branch = instructs->count;

		int stack_size_yes = compile(*e.condition.yes, instructs, s, stack_size);

		// Don't know how to do in a different way for now.
		assert(stack_size_yes == stack_size_no);

		instruction *jump = instructs->array + jump_instruction_position;
		assert(jump->is == is_jump);
		jump->jump = instructs->count;
	} break;
	}
	return stack_size;
}

typedef struct stack {
	int *values;
	int size;
	int capacity;
} stack;

void stack_push(stack *s, int i) {
	if (s->size == s->capacity) {
		int new_capacity = s->capacity * 2 + 1;
		s->values = realloc(s->values, new_capacity * sizeof (int));
		s->capacity = new_capacity;
	}
	s->values[s->size] = i;
	s->size++;
}

int stack_pop(stack *s) {
	assert(s->size != 0);
	s->size--;
	return s->values[s->size];
}

int interpret(instructions instructs, stack *s, _Bool loud) {
	if (loud) printf("The interpretation steps:\n\n");
	for (int i = 0; i < instructs.count; i++) {
		instruction ins = instructs.array[i];
		switch (ins.is) {
		case is_load_literal:
			if (loud) printf("\tload: %d\n", ins.load_literal);
			stack_push(s, ins.load_literal);
		break;
		case is_add: {
			int x = stack_pop(s);
			int y = stack_pop(s);
			if (loud) printf("\tadd: %d, %d\n", x, y);
			stack_push(s, x + y);
		} break;
		case is_copy:
			if (loud)
				printf("\tcopy: [%d] = %d\n", ins.copy, s->values[ins.copy]);
			stack_push(s, s->values[ins.copy]);
		break;
		case is_drop:
			if (loud)
				printf("\tdrop: [%d] = %d\n", ins.drop, s->values[ins.drop]);
			for (int i = ins.drop; i < s->size - 1; i++)
				s->values[i] = s->values[i + 1];
			s->size--;
		break;
		case is_branch: {
			int test = stack_pop(s);
			if (loud) printf("\tbranch: [%d] = %d\n", ins.branch, test);
			if (test) i = ins.branch - 1;
		} break;
		case is_jump:
			if (loud) printf("\tjump: [%d]\n", ins.jump);
			i = ins.jump - 1;
		break;
		}
	}

	// Commented out since branch was added.
	// assert(s->size == 1);

	assert(s->size != 0);
	int result = s->values[s->size - 1];
	free(s->values);

	if (loud) printf("\n");

	return result;
}

#define make_instructions(...) ( \
	(instructions){ \
		(instruction[]){ __VA_ARGS__ }, \
		sizeof (instruction[]){ __VA_ARGS__ } / sizeof (instruction), \
	} \
)

int main(void) {
	for (int i = 0; i < sizeof programs / sizeof *programs; i++) {
		expression *program = programs + i;

		list_expression(*program);

		printf(
			"The result of interpretation of the tree: \n\n\t%d\n\n",
			walk(*program, 0)
		);

		instructions instructs = { 0 };
		int stack_size = compile(*program, &instructs, 0, 0);

		// Commented out since branch was added.
		// assert(stack_size == 1);

		list_instructions(instructs);

		printf(
			"The result of interpretation of the byte code: \n\n\t%d\n\n",
			interpret(instructs, &(stack){ 0 }, 1)
		);

		printf("\n");
	}
}
