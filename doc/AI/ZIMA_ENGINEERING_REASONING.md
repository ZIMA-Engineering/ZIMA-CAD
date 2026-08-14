# ZIMA Engineering Reasoning

## Purpose

This document defines a general reasoning method for engineering design.

It does not describe specific mechanisms, materials, manufacturing methods,
or machine elements.

It describes HOW an engineering problem should be approached.

The method is intended for both human engineers and AI engineering agents.

---

# 1. Fundamental Model

Every engineering problem shall first be reduced to three fundamental groups:

INPUTS → MEANS → OUTPUTS

## Inputs

Everything entering or affecting the system.

Examples:

- force
- torque
- energy
- motion
- material
- information
- environment
- human interaction
- existing geometry
- external constraints

## Outputs

Everything the system is required to produce.

Examples:

- force
- motion
- position
- accuracy
- transformed material
- transported object
- heat
- information
- completed operation

## Means

Everything available to transform the inputs into the required outputs.

Means include not only physical components but also the resources and
constraints available to the designer.

Examples:

- physical principles
- mechanisms
- energy sources
- materials
- available space
- existing components
- manufacturing technologies
- manufacturing machines
- assembly capabilities
- measurement capabilities
- human capabilities
- time
- cost

Manufacturing means are part of the engineering problem.

A theoretically valid solution that cannot reasonably be manufactured with
the available means may not be a valid engineering solution.

---

# 2. First Loop — Measure

Before attempting to design anything, evaluate:

INPUTS → MEANS → OUTPUTS

and ask:

Does this problem have reasonable measure?

Do not automatically trust the specification.

Human estimates may be wrong by an order of magnitude or more.

Perform independent approximate checks of:

- dimensions
- mass
- force
- torque
- power
- energy
- speed
- acceleration
- accuracy
- stiffness
- manufacturing capability
- cost
- time

Exact calculation is not always necessary at this stage.

The objective is to determine whether the problem is located in a physically
and practically reasonable region.

A physically possible requirement may still have unreasonable measure.

If the input/output relationship is unreasonable, question the specification
before designing the mechanism.

---

# 3. Knowledge Is Not the Solution

The engineering agent may possess a large database of existing solutions.

This may include:

- known mechanisms
- machine elements
- previous machines
- historical designs
- manufacturing methods
- standards
- materials
- catalog components
- previous engineering experience

This knowledge is valuable.

However:

KNOWLEDGE MUST NOT FORCE THE DESIGN TOWARD THE AVERAGE SOLUTION.

Common practice is evidence, not proof.

A solution must not be selected merely because:

- it is commonly used,
- most engineers use it,
- similar machines use it,
- it has historically been used.

Existing solutions provide:

- reference
- scale
- known working principles
- known failure modes
- reusable concepts

They do not define the complete solution space.

Knowledge should constrain and verify reality.

Knowledge should not imprison invention.

---

# 4. Search for an Existing Solution

First determine whether a known solution already satisfies:

INPUTS → MEANS → OUTPUTS

If a simple, proven solution satisfies the problem well, use it.

Novelty has no value by itself.

Do not invent a new mechanism merely because invention is possible.

A good existing solution is still a good solution.

---

# 5. Combination

If no existing solution satisfies the problem, combine known principles.

Combination does not mean simply joining complete mechanisms.

A new design may combine abstract properties from unrelated systems.

For example:

- motion principle from one mechanism
- force transmission from another
- manufacturing principle from another
- compliance principle from another
- locking principle from another
- geometry from another

The resulting system may have never existed before even though some of its
principles are known.

Every combination must again be evaluated through:

INPUTS → MEANS → OUTPUTS

---

# 6. Failure of Conventional Search

Conventional reasoning explores a region around known solutions.

This is useful but limited.

If repeated conventional modifications and combinations do not produce a
satisfactory solution, continuing the same search may only optimize the wrong
region.

At this point the search must be allowed to leave the current solution region.

---

# 7. The Error / Jump

A mistake is not necessarily only a failure.

A mistake can move reasoning into a region of the solution space that correct,
predictable reasoning would never explore.

Human invention can sometimes originate from:

- incorrect association
- incorrect assumption
- accidental transformation
- misunderstanding
- unexpected geometry
- unexpected combination
- mental error
- intuition
- unexplained insight

The origin of the candidate is not important.

Its validity is.

For an AI system, this behaviour may be approximated by deliberately making
jumps away from the statistically most probable solution space.

The jump MUST NOT be treated as a valid solution.

It produces only a new candidate point.

---

# 8. Evaluation After a Jump

Immediately evaluate the new candidate using:

INPUTS → MEANS → OUTPUTS

Perform inexpensive checks first.

Reject candidates that obviously violate:

- physics
- fundamental geometry
- required function
- available means
- manufacturing reality
- critical constraints

Do not fully engineer obviously invalid candidates.

The purpose of this stage is to determine whether the jump has entered a
potentially useful region.

---

# 9. Local Vectors

If a new candidate appears potentially viable, do not immediately accept it.

Explore the local solution space around it.

Create at least three useful independent variation vectors from the candidate
point.

Conceptually:

X → X + V1
X → X + V2
X → X + V3

More vectors may be required for complex problems.

The vectors do not have to represent geometric dimensions.

A dimension of the solution space may represent:

- geometry
- mass
- force
- speed
- stiffness
- material
- manufacturing process
- mechanism principle
- number of components
- energy source
- accuracy
- cost
- assembly method
- control strategy
- another engineering property

Evaluate each resulting state through:

INPUTS → MEANS → OUTPUTS

Determine whether movement in each direction:

- improves the solution
- degrades the solution
- leaves it approximately unchanged
- causes the principle to stop functioning

---

# 10. Measure as a Multidimensional Region

A valid engineering principle should not be understood only as a single
working point.

It exists inside a multidimensional region in which it remains useful.

This region can be imagined as a "bubble" in N-dimensional solution space.

Inside the bubble:

the principle works.

At the boundary:

one or more constraints approach their limits.

Outside the bubble:

the principle becomes impractical or stops working.

This region defines the MEASURE of the solution.

Measure is therefore not only a numerical size.

It is the range of conditions within which a principle remains valid.

---

# 11. Multiple Solution Bubbles

A problem may contain several disconnected valid regions.

For example:

          [ Solution A ]



                         [ Solution B ]


      [ Solution C ]

The space between these regions may contain no useful solutions.

Local optimization inside Solution A cannot necessarily discover Solution B.

This is why the ability to jump is important.

A jump may cross an invalid region and discover another valid solution region.

The new region may represent an entirely different engineering principle.

---

# 12. Repeated Jumps

If a newly discovered region does not provide a satisfactory solution:

jump again.

The process may therefore become:

known region
    ↓
local search
    ↓
failure
    ↓
JUMP
    ↓
candidate
    ↓
basic evaluation
    ↓
local vectors
    ↓
new region
    ↓
evaluation
    ↓
JUMP AGAIN

There is no requirement that the final solution remain conceptually close to
the initial solution.

---

# 13. The Role of Error in Invention

An engineering intelligence should distinguish between two different goals:

RELIABILITY

and

INVENTION

Reliability attempts to eliminate errors.

Invention cannot always do so.

If every step is forced to remain inside the statistically most probable,
previously validated solution space, the system may become extremely competent
while producing little that is fundamentally new.

Therefore:

ERROR MUST NOT AUTOMATICALLY BE ELIMINATED DURING THE GENERATION OF NEW
CANDIDATES.

Error, deviation and unexpected states may provide paths into previously
unexplored solution regions.

After generation, however, candidates must be subjected to rigorous
engineering evaluation.

In short:

ALLOW ERROR DURING DISCOVERY.

DO NOT ALLOW ERROR DURING VERIFICATION.

---

# 14. Hierarchical Application

The same reasoning can be recursively applied to any level of a machine.

A complete machine can be described as:

INPUTS → MEANS → OUTPUTS

A subsystem inside the machine can also be described as:

INPUTS → MEANS → OUTPUTS

A mechanism inside that subsystem can again be described as:

INPUTS → MEANS → OUTPUTS

Therefore complex engineering problems can be decomposed recursively while
preserving the same reasoning method.

---

# 15. Verification

A creative solution is not automatically a good solution.

Every final candidate must be verified using appropriate engineering methods.

Depending on the problem this may include:

- analytical calculations
- numerical simulation
- geometry checks
- tolerance analysis
- collision checks
- kinematic simulation
- strength analysis
- thermal analysis
- fatigue analysis
- manufacturing analysis
- assembly analysis
- prototype testing
- physical measurement

Creativity generates candidates.

Engineering verification decides whether they survive.

---

# 16. Learning From Reality

After a design is manufactured or tested, compare reality with the predicted
behaviour.

The difference is valuable information.

Use it to improve the estimated boundaries of the solution region.

Therefore the complete process contains feedback:

DESIGN
  ↓
SIMULATION / MANUFACTURE / TEST
  ↓
REAL RESULT
  ↓
COMPARE WITH EXPECTATION
  ↓
UPDATE KNOWLEDGE AND MEASURE

The purpose is not merely to remember whether one design worked.

The purpose is to improve understanding of WHERE and WHY the underlying
principle works.

---

# 17. AI Engineering Principle

An AI engineering system should separate:

KNOWLEDGE

from

INVENTION.

Knowledge provides:

- physical reality
- known mechanisms
- engineering experience
- manufacturing knowledge
- approximate measure
- verification

Invention requires the ability to leave the most probable solution path.

Therefore an AI engineer should not simply ask:

"What solution is most probable according to what I know?"

It should also be capable of asking:

"What other region of the solution space might satisfy the same
Inputs → Means → Outputs relationship?"

---

# 18. Fundamental Loop

The complete reasoning loop can be summarized as:

1. Define INPUTS.
2. Define required OUTPUTS.
3. Define available MEANS.
4. Check MEASURE.
5. Search known solutions.
6. If appropriate, use a known solution.
7. Otherwise combine known principles.
8. Evaluate.
9. If the search becomes trapped, JUMP.
10. Evaluate the jumped candidate cheaply.
11. If promising, explore several local vectors.
12. Determine the approximate valid region ("bubble").
13. Evaluate the region against INPUTS → MEANS → OUTPUTS.
14. Optimize inside a useful region.
15. If necessary, jump to another region.
16. Verify the final solution rigorously.
17. Compare the real result with prediction.
18. Learn the limits of the principle.

---

# 19. Core Rules

## Rule 1

Always evaluate engineering solutions through:

INPUTS → MEANS → OUTPUTS

## Rule 2

Check measure before detailed design.

## Rule 3

Do not blindly trust human estimates or specifications.

## Rule 4

Use existing engineering knowledge as a reference, not as an authority.

## Rule 5

Do not confuse statistical probability with engineering correctness.

## Rule 6

Do not invent when a simple proven solution already satisfies the problem.

## Rule 7

When necessary, combine principles rather than complete existing solutions.

## Rule 8

When conventional reasoning is trapped, leave the current solution region.

## Rule 9

Allow error and unexpected deviation to generate new candidate regions.

## Rule 10

After a jump, return immediately to rigorous engineering evaluation.

## Rule 11

Explore a promising candidate in multiple independent directions.

## Rule 12

Understand a working principle as a multidimensional region of validity,
not merely a single solution point.

## Rule 13

Expect multiple disconnected valid regions to exist.

## Rule 14

Knowledge should verify invention, not prevent it.

## Rule 15

A new idea earns no special protection because it is new.

Reality has the final vote.


ALLOW ERROR DURING DISCOVERY. DO NOT ALLOW ERROR DURING VERIFICATION.
