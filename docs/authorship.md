# Authorship and methodology

## LLM-assisted documentation

Large language models were used heavily in producing this documentation. They
helped reduce repetitive editorial work, reorganize material, draft explanatory
text, and transfer language-design notes into a consistent documentation
structure.

This use of LLMs was intended to save time and reduce the grunt work involved in
maintaining a detailed specification. LLM-generated text should not be treated
as an independent source of language requirements.

## Human and LLM involvement in the specification

The Vortex language specification was developed through a combination of human
intervention and LLM assistance. Human review is used to make and resolve design
decisions, compare rules across chapters, inspect the formal grammar, review
examples, and identify inconsistencies introduced during drafting or transfer.

The normative requirements are the requirements written in this specification,
regardless of whether a passage began as human-written or LLM-assisted text.
Where pages conflict,
[Document authority](specification/conformance.md#11-document-authority)
decides which is right ([record 49](decisions/documentation.md#d49)).

## Consistency and corrections

The goal of the combined review process is to keep the grammar, prose, examples,
and implementation guidance consistent. Because Vortex is evolving, this process
cannot guarantee that every error has already been found.

When a contradiction is discovered, it should be resolved in the normative
specification and reflected in the language guide, compiler documentation, and
tests. Repository issues and focused corrections are part of that review
process.
