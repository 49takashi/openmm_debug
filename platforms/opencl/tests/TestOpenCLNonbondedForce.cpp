/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2008-2010 Stanford University and the Authors.      *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

/**
 * This tests all the different force terms in the reference implementation of NonbondedForce.
 */

#include "../../../tests/AssertionUtilities.h"
#include "openmm/Context.h"
#include "OpenCLPlatform.h"
#include "ReferencePlatform.h"
#include "openmm/HarmonicBondForce.h"
#include "openmm/NonbondedForce.h"
#include "openmm/System.h"
#include "openmm/LangevinIntegrator.h"
#include "openmm/VerletIntegrator.h"
#include "openmm/internal/ContextImpl.h"
#include "OpenCLArray.h"
#include "OpenCLNonbondedUtilities.h"
#include "../src/SimTKUtilities/SimTKOpenMMRealType.h"
#include "sfmt/SFMT.h"
#include <iostream>
#include <vector>

using namespace OpenMM;
using namespace std;

const double TOL = 1e-5;

void testCoulomb() {
    OpenCLPlatform platform;
    System system;
    system.addParticle(1.0);
    system.addParticle(1.0);
    LangevinIntegrator integrator(0.0, 0.1, 0.01);
    NonbondedForce* forceField = new NonbondedForce();
    forceField->addParticle(0.5, 1, 0);
    forceField->addParticle(-1.5, 1, 0);
    system.addForce(forceField);
    Context context(system, integrator, platform);
    vector<Vec3> positions(2);
    positions[0] = Vec3(0, 0, 0);
    positions[1] = Vec3(2, 0, 0);
    context.setPositions(positions);
    State state = context.getState(State::Forces | State::Energy);
    const vector<Vec3>& forces = state.getForces();
    double force = ONE_4PI_EPS0*(-0.75)/4.0;
    ASSERT_EQUAL_VEC(Vec3(-force, 0, 0), forces[0], TOL);
    ASSERT_EQUAL_VEC(Vec3(force, 0, 0), forces[1], TOL);
    ASSERT_EQUAL_TOL(ONE_4PI_EPS0*(-0.75)/2.0, state.getPotentialEnergy(), TOL);
}

void testLJ() {
    OpenCLPlatform platform;
    System system;
    system.addParticle(1.0);
    system.addParticle(1.0);
    LangevinIntegrator integrator(0.0, 0.1, 0.01);
    NonbondedForce* forceField = new NonbondedForce();
    forceField->addParticle(0, 1.2, 1);
    forceField->addParticle(0, 1.4, 2);
    system.addForce(forceField);
    Context context(system, integrator, platform);
    vector<Vec3> positions(2);
    positions[0] = Vec3(0, 0, 0);
    positions[1] = Vec3(2, 0, 0);
    context.setPositions(positions);
    State state = context.getState(State::Forces | State::Energy);
    const vector<Vec3>& forces = state.getForces();
    double x = 1.3/2.0;
    double eps = SQRT_TWO;
    double force = 4.0*eps*(12*std::pow(x, 12.0)-6*std::pow(x, 6.0))/2.0;
    ASSERT_EQUAL_VEC(Vec3(-force, 0, 0), forces[0], TOL);
    ASSERT_EQUAL_VEC(Vec3(force, 0, 0), forces[1], TOL);
    ASSERT_EQUAL_TOL(4.0*eps*(std::pow(x, 12.0)-std::pow(x, 6.0)), state.getPotentialEnergy(), TOL);
}

void testExclusionsAnd14() {
    OpenCLPlatform platform;
    System system;
    NonbondedForce* nonbonded = new NonbondedForce();
    for (int i = 0; i < 5; ++i) {
        system.addParticle(1.0);
        nonbonded->addParticle(0, 1.5, 0);
    }
    vector<pair<int, int> > bonds;
    bonds.push_back(pair<int, int>(0, 1));
    bonds.push_back(pair<int, int>(1, 2));
    bonds.push_back(pair<int, int>(2, 3));
    bonds.push_back(pair<int, int>(3, 4));
    nonbonded->createExceptionsFromBonds(bonds, 0.0, 0.0);
    int first14, second14;
    for (int i = 0; i < nonbonded->getNumExceptions(); i++) {
        int particle1, particle2;
        double chargeProd, sigma, epsilon;
        nonbonded->getExceptionParameters(i, particle1, particle2, chargeProd, sigma, epsilon);
        if ((particle1 == 0 && particle2 == 3) || (particle1 == 3 && particle2 == 0))
            first14 = i;
        if ((particle1 == 1 && particle2 == 4) || (particle1 == 4 && particle2 == 1))
            second14 = i;
    }
    system.addForce(nonbonded);
    for (int i = 1; i < 5; ++i) {

        // Test LJ forces

        vector<Vec3> positions(5);
        const double r = 1.0;
        for (int j = 0; j < 5; ++j) {
            nonbonded->setParticleParameters(j, 0, 1.5, 0);
            positions[j] = Vec3(0, j, 0);
        }
        nonbonded->setParticleParameters(0, 0, 1.5, 1);
        nonbonded->setParticleParameters(i, 0, 1.5, 1);
        nonbonded->setExceptionParameters(first14, 0, 3, 0, 1.5, i == 3 ? 0.5 : 0.0);
        nonbonded->setExceptionParameters(second14, 1, 4, 0, 1.5, 0.0);
        positions[i] = Vec3(r, 0, 0);
        LangevinIntegrator integrator(0.0, 0.1, 0.01);
        Context context(system, integrator, platform);
        context.setPositions(positions);
        State state = context.getState(State::Forces | State::Energy);
        const vector<Vec3>& forces = state.getForces();
        double x = 1.5/r;
        double eps = 1.0;
        double force = 4.0*eps*(12*std::pow(x, 12.0)-6*std::pow(x, 6.0))/r;
        double energy = 4.0*eps*(std::pow(x, 12.0)-std::pow(x, 6.0));
        if (i == 3) {
            force *= 0.5;
            energy *= 0.5;
        }
        if (i < 3) {
            force = 0;
            energy = 0;
        }
        ASSERT_EQUAL_VEC(Vec3(-force, 0, 0), forces[0], TOL);
        ASSERT_EQUAL_VEC(Vec3(force, 0, 0), forces[i], TOL);
        ASSERT_EQUAL_TOL(energy, state.getPotentialEnergy(), TOL);

        // Test Coulomb forces

        nonbonded->setParticleParameters(0, 2, 1.5, 0);
        nonbonded->setParticleParameters(i, 2, 1.5, 0);
        nonbonded->setExceptionParameters(first14, 0, 3, i == 3 ? 4/1.2 : 0, 1.5, 0);
        nonbonded->setExceptionParameters(second14, 1, 4, 0, 1.5, 0);
        LangevinIntegrator integrator2(0.0, 0.1, 0.01);
        Context context2(system, integrator2, platform);
        context2.setPositions(positions);
        state = context2.getState(State::Forces | State::Energy);
        const vector<Vec3>& forces2 = state.getForces();
        force = ONE_4PI_EPS0*4/(r*r);
        energy = ONE_4PI_EPS0*4/r;
        if (i == 3) {
            force /= 1.2;
            energy /= 1.2;
        }
        if (i < 3) {
            force = 0;
            energy = 0;
        }
        ASSERT_EQUAL_VEC(Vec3(-force, 0, 0), forces2[0], TOL);
        ASSERT_EQUAL_VEC(Vec3(force, 0, 0), forces2[i], TOL);
        ASSERT_EQUAL_TOL(energy, state.getPotentialEnergy(), TOL);
    }
}

void testCutoff() {
    OpenCLPlatform platform;
    System system;
    system.addParticle(1.0);
    system.addParticle(1.0);
    system.addParticle(1.0);
    LangevinIntegrator integrator(0.0, 0.1, 0.01);
    NonbondedForce* forceField = new NonbondedForce();
    forceField->addParticle(1.0, 1, 0);
    forceField->addParticle(1.0, 1, 0);
    forceField->addParticle(1.0, 1, 0);
    forceField->setNonbondedMethod(NonbondedForce::CutoffNonPeriodic);
    const double cutoff = 2.9;
    forceField->setCutoffDistance(cutoff);
    const double eps = 50.0;
    forceField->setReactionFieldDielectric(eps);
    system.addForce(forceField);
    Context context(system, integrator, platform);
    vector<Vec3> positions(3);
    positions[0] = Vec3(0, 0, 0);
    positions[1] = Vec3(0, 2, 0);
    positions[2] = Vec3(0, 3, 0);
    context.setPositions(positions);
    State state = context.getState(State::Forces | State::Energy);
    const vector<Vec3>& forces = state.getForces();
    const double krf = (1.0/(cutoff*cutoff*cutoff))*(eps-1.0)/(2.0*eps+1.0);
    const double crf = (1.0/cutoff)*(3.0*eps)/(2.0*eps+1.0);
    const double force1 = ONE_4PI_EPS0*(1.0)*(0.25-2.0*krf*2.0);
    const double force2 = ONE_4PI_EPS0*(1.0)*(1.0-2.0*krf*1.0);
    ASSERT_EQUAL_VEC(Vec3(0, -force1, 0), forces[0], TOL);
    ASSERT_EQUAL_VEC(Vec3(0, force1-force2, 0), forces[1], TOL);
    ASSERT_EQUAL_VEC(Vec3(0, force2, 0), forces[2], TOL);
    const double energy1 = ONE_4PI_EPS0*(1.0)*(0.5+krf*4.0-crf);
    const double energy2 = ONE_4PI_EPS0*(1.0)*(1.0+krf*1.0-crf);
    ASSERT_EQUAL_TOL(energy1+energy2, state.getPotentialEnergy(), TOL);
}

void testCutoff14() {
    OpenCLPlatform platform;
    System system;
    LangevinIntegrator integrator(0.0, 0.1, 0.01);
    NonbondedForce* nonbonded = new NonbondedForce();
    nonbonded->setNonbondedMethod(NonbondedForce::CutoffNonPeriodic);
    for (int i = 0; i < 5; ++i) {
        system.addParticle(1.0);
        nonbonded->addParticle(0, 1.5, 0);
    }
    const double cutoff = 3.5;
    nonbonded->setCutoffDistance(cutoff);
    const double eps = 30.0;
    nonbonded->setReactionFieldDielectric(eps);
    vector<pair<int, int> > bonds;
    bonds.push_back(pair<int, int>(0, 1));
    bonds.push_back(pair<int, int>(1, 2));
    bonds.push_back(pair<int, int>(2, 3));
    bonds.push_back(pair<int, int>(3, 4));
    nonbonded->createExceptionsFromBonds(bonds, 0.0, 0.0);
    int first14, second14;
    for (int i = 0; i < nonbonded->getNumExceptions(); i++) {
        int particle1, particle2;
        double chargeProd, sigma, epsilon;
        nonbonded->getExceptionParameters(i, particle1, particle2, chargeProd, sigma, epsilon);
        if ((particle1 == 0 && particle2 == 3) || (particle1 == 3 && particle2 == 0))
            first14 = i;
        if ((particle1 == 1 && particle2 == 4) || (particle1 == 4 && particle2 == 1))
            second14 = i;
    }
    system.addForce(nonbonded);
    Context context(system, integrator, platform);
    vector<Vec3> positions(5);
    positions[0] = Vec3(0, 0, 0);
    positions[1] = Vec3(1, 0, 0);
    positions[2] = Vec3(2, 0, 0);
    positions[3] = Vec3(3, 0, 0);
    positions[4] = Vec3(4, 0, 0);
    for (int i = 1; i < 5; ++i) {

        // Test LJ forces

        nonbonded->setParticleParameters(0, 0, 1.5, 1);
        for (int j = 1; j < 5; ++j)
            nonbonded->setParticleParameters(j, 0, 1.5, 0);
        nonbonded->setParticleParameters(i, 0, 1.5, 1);
        nonbonded->setExceptionParameters(first14, 0, 3, 0, 1.5, i == 3 ? 0.5 : 0.0);
        nonbonded->setExceptionParameters(second14, 1, 4, 0, 1.5, 0.0);
        context.reinitialize();
        context.setPositions(positions);
        State state = context.getState(State::Forces | State::Energy);
        const vector<Vec3>& forces = state.getForces();
        double r = positions[i][0];
        double x = 1.5/r;
        double e = 1.0;
        double force = 4.0*e*(12*std::pow(x, 12.0)-6*std::pow(x, 6.0))/r;
        double energy = 4.0*e*(std::pow(x, 12.0)-std::pow(x, 6.0));
        if (i == 3) {
            force *= 0.5;
            energy *= 0.5;
        }
        if (i < 3 || r > cutoff) {
            force = 0;
            energy = 0;
        }
        ASSERT_EQUAL_VEC(Vec3(-force, 0, 0), forces[0], TOL);
        ASSERT_EQUAL_VEC(Vec3(force, 0, 0), forces[i], TOL);
        ASSERT_EQUAL_TOL(energy, state.getPotentialEnergy(), TOL);

        // Test Coulomb forces

        const double q = 0.7;
        nonbonded->setParticleParameters(0, q, 1.5, 0);
        nonbonded->setParticleParameters(i, q, 1.5, 0);
        nonbonded->setExceptionParameters(first14, 0, 3, i == 3 ? q*q/1.2 : 0, 1.5, 0);
        nonbonded->setExceptionParameters(second14, 1, 4, 0, 1.5, 0);
        context.reinitialize();
        context.setPositions(positions);
        state = context.getState(State::Forces | State::Energy);
        const vector<Vec3>& forces2 = state.getForces();
        force = ONE_4PI_EPS0*q*q/(r*r);
        energy = ONE_4PI_EPS0*q*q/r;
        if (i == 3) {
            force /= 1.2;
            energy /= 1.2;
        }
        if (i < 3 || r > cutoff) {
            force = 0;
            energy = 0;
        }
        ASSERT_EQUAL_VEC(Vec3(-force, 0, 0), forces2[0], TOL);
        ASSERT_EQUAL_VEC(Vec3(force, 0, 0), forces2[i], TOL);
        ASSERT_EQUAL_TOL(energy, state.getPotentialEnergy(), TOL);
    }
}

void testPeriodic() {
    OpenCLPlatform platform;
    System system;
    system.addParticle(1.0);
    system.addParticle(1.0);
    system.addParticle(1.0);
    LangevinIntegrator integrator(0.0, 0.1, 0.01);
    NonbondedForce* nonbonded = new NonbondedForce();
    nonbonded->addParticle(1.0, 1, 0);
    nonbonded->addParticle(1.0, 1, 0);
    nonbonded->addParticle(1.0, 1, 0);
    nonbonded->addException(0, 1, 0.0, 1.0, 0.0);
    nonbonded->setNonbondedMethod(NonbondedForce::CutoffPeriodic);
    const double cutoff = 2.0;
    nonbonded->setCutoffDistance(cutoff);
    system.setDefaultPeriodicBoxVectors(Vec3(4, 0, 0), Vec3(0, 4, 0), Vec3(0, 0, 4));
    system.addForce(nonbonded);
    Context context(system, integrator, platform);
    vector<Vec3> positions(3);
    positions[0] = Vec3(0, 0, 0);
    positions[1] = Vec3(2, 0, 0);
    positions[2] = Vec3(3, 0, 0);
    context.setPositions(positions);
    State state = context.getState(State::Forces | State::Energy);
    const vector<Vec3>& forces = state.getForces();
    const double eps = 78.3;
    const double krf = (1.0/(cutoff*cutoff*cutoff))*(eps-1.0)/(2.0*eps+1.0);
    const double crf = (1.0/cutoff)*(3.0*eps)/(2.0*eps+1.0);
    const double force = ONE_4PI_EPS0*(1.0)*(1.0-2.0*krf*1.0);
    ASSERT_EQUAL_VEC(Vec3(force, 0, 0), forces[0], TOL);
    ASSERT_EQUAL_VEC(Vec3(-force, 0, 0), forces[1], TOL);
    ASSERT_EQUAL_VEC(Vec3(0, 0, 0), forces[2], TOL);
    ASSERT_EQUAL_TOL(2*ONE_4PI_EPS0*(1.0)*(1.0+krf*1.0-crf), state.getPotentialEnergy(), TOL);
}


void testLargeSystem() {
    const int numMolecules = 600;
    const int numParticles = numMolecules*2;
    const double cutoff = 2.0;
    const double boxSize = 20.0;
    const double tol = 2e-3;
    OpenCLPlatform cl;
    ReferencePlatform reference;
    System system;
    for (int i = 0; i < numParticles; i++)
        system.addParticle(1.0);
    NonbondedForce* nonbonded = new NonbondedForce();
    HarmonicBondForce* bonds = new HarmonicBondForce();
    vector<Vec3> positions(numParticles);
    vector<Vec3> velocities(numParticles);
    OpenMM_SFMT::SFMT sfmt;
    init_gen_rand(0, sfmt);

    for (int i = 0; i < numMolecules; i++) {
        if (i < numMolecules/2) {
            nonbonded->addParticle(-1.0, 0.2, 0.1);
            nonbonded->addParticle(1.0, 0.1, 0.1);
        }
        else {
            nonbonded->addParticle(-1.0, 0.2, 0.2);
            nonbonded->addParticle(1.0, 0.1, 0.2);
        }
        positions[2*i] = Vec3(boxSize*genrand_real2(sfmt), boxSize*genrand_real2(sfmt), boxSize*genrand_real2(sfmt));
        positions[2*i+1] = Vec3(positions[2*i][0]+1.0, positions[2*i][1], positions[2*i][2]);
        velocities[2*i] = Vec3(genrand_real2(sfmt), genrand_real2(sfmt), genrand_real2(sfmt));
        velocities[2*i+1] = Vec3(genrand_real2(sfmt), genrand_real2(sfmt), genrand_real2(sfmt));
        bonds->addBond(2*i, 2*i+1, 1.0, 0.1);
        nonbonded->addException(2*i, 2*i+1, 0.0, 0.15, 0.0);
    }

    // Try with cutoffs but not periodic boundary conditions, and make sure the cl and Reference
    // platforms agree.

    nonbonded->setNonbondedMethod(NonbondedForce::CutoffNonPeriodic);
    nonbonded->setCutoffDistance(cutoff);
    system.addForce(nonbonded);
    system.addForce(bonds);
    VerletIntegrator integrator1(0.01);
    VerletIntegrator integrator2(0.01);
    Context clContext(system, integrator1, cl);
    Context referenceContext(system, integrator2, reference);
    clContext.setPositions(positions);
    clContext.setVelocities(velocities);
    referenceContext.setPositions(positions);
    referenceContext.setVelocities(velocities);
    State clState = clContext.getState(State::Positions | State::Velocities | State::Forces | State::Energy);
    State referenceState = referenceContext.getState(State::Positions | State::Velocities | State::Forces | State::Energy);
    for (int i = 0; i < numParticles; i++) {
        ASSERT_EQUAL_VEC(clState.getPositions()[i], referenceState.getPositions()[i], tol);
        ASSERT_EQUAL_VEC(clState.getVelocities()[i], referenceState.getVelocities()[i], tol);
        ASSERT_EQUAL_VEC(clState.getForces()[i], referenceState.getForces()[i], tol);
    }
    ASSERT_EQUAL_TOL(clState.getPotentialEnergy(), referenceState.getPotentialEnergy(), tol);

    // Now do the same thing with periodic boundary conditions.

    nonbonded->setNonbondedMethod(NonbondedForce::CutoffPeriodic);
    system.setDefaultPeriodicBoxVectors(Vec3(boxSize, 0, 0), Vec3(0, boxSize, 0), Vec3(0, 0, boxSize));
    clContext.reinitialize();
    referenceContext.reinitialize();
    clContext.setPositions(positions);
    clContext.setVelocities(velocities);
    referenceContext.setPositions(positions);
    referenceContext.setVelocities(velocities);
    clState = clContext.getState(State::Positions | State::Velocities | State::Forces | State::Energy);
    referenceState = referenceContext.getState(State::Positions | State::Velocities | State::Forces | State::Energy);
    for (int i = 0; i < numParticles; i++) {
        ASSERT_EQUAL_TOL(fmod(clState.getPositions()[i][0]-referenceState.getPositions()[i][0], boxSize), 0, tol);
        ASSERT_EQUAL_TOL(fmod(clState.getPositions()[i][1]-referenceState.getPositions()[i][1], boxSize), 0, tol);
        ASSERT_EQUAL_TOL(fmod(clState.getPositions()[i][2]-referenceState.getPositions()[i][2], boxSize), 0, tol);
        ASSERT_EQUAL_VEC(clState.getVelocities()[i], referenceState.getVelocities()[i], tol);
        ASSERT_EQUAL_VEC(clState.getForces()[i], referenceState.getForces()[i], tol);
    }
    ASSERT_EQUAL_TOL(clState.getPotentialEnergy(), referenceState.getPotentialEnergy(), tol);
}

void testBlockInteractions(bool periodic) {
    const int blockSize = 32;
    const int numBlocks = 100;
    const int numParticles = blockSize*numBlocks;
    const double cutoff = 1.0;
    const double boxSize = (periodic ? 5.1 : 1.1);
    OpenCLPlatform cl;
    System system;
    VerletIntegrator integrator(0.01);
    NonbondedForce* nonbonded = new NonbondedForce();
    vector<Vec3> positions(numParticles);
    OpenMM_SFMT::SFMT sfmt;
    init_gen_rand(0, sfmt);

    for (int i = 0; i < numParticles; i++) {
        system.addParticle(1.0);
        nonbonded->addParticle(1.0, 0.2, 0.2);
        positions[i] = Vec3(boxSize*(3*genrand_real2(sfmt)-1), boxSize*(3*genrand_real2(sfmt)-1), boxSize*(3*genrand_real2(sfmt)-1));
    }
    nonbonded->setNonbondedMethod(periodic ? NonbondedForce::CutoffPeriodic : NonbondedForce::CutoffNonPeriodic);
    nonbonded->setCutoffDistance(cutoff);
    system.setDefaultPeriodicBoxVectors(Vec3(boxSize, 0, 0), Vec3(0, boxSize, 0), Vec3(0, 0, boxSize));
    system.addForce(nonbonded);
    Context context(system, integrator, cl);
    context.setPositions(positions);
    ContextImpl* contextImpl = *reinterpret_cast<ContextImpl**>(&context);
    OpenCLPlatform::PlatformData& data = *static_cast<OpenCLPlatform::PlatformData*>(contextImpl->getPlatformData());
    OpenCLContext& clcontext = *data.contexts[0];
    OpenCLNonbondedUtilities& nb = clcontext.getNonbondedUtilities();
    State state = context.getState(State::Positions | State::Velocities | State::Forces);
    nb.updateNeighborListSize();
    state = context.getState(State::Positions | State::Velocities | State::Forces);

    // Verify that the bounds of each block were calculated correctly.

    clcontext.getPosq().download();
    vector<mm_float4> blockCenters(numBlocks);
    vector<mm_float4> blockBoundingBoxes(numBlocks);
    nb.getBlockCenters().download(blockCenters);
    nb.getBlockBoundingBoxes().download(blockBoundingBoxes);
    for (int i = 0; i < numBlocks; i++) {
        mm_float4 gridSize = blockBoundingBoxes[i];
        mm_float4 center = blockCenters[i];
        if (periodic) {
            ASSERT(gridSize.x < 0.5*boxSize);
            ASSERT(gridSize.y < 0.5*boxSize);
            ASSERT(gridSize.z < 0.5*boxSize);
        }
        float minx = 0.0, maxx = 0.0, miny = 0.0, maxy = 0.0, minz = 0.0, maxz = 0.0, radius = 0.0;
        for (int j = 0; j < blockSize; j++) {
            mm_float4 pos = clcontext.getPosq()[i*blockSize+j];
            float dx = pos.x-center.x;
            float dy = pos.y-center.y;
            float dz = pos.z-center.z;
            if (periodic) {
                dx -= (float)(floor(0.5+dx/boxSize)*boxSize);
                dy -= (float)(floor(0.5+dy/boxSize)*boxSize);
                dz -= (float)(floor(0.5+dz/boxSize)*boxSize);
            }
            ASSERT(abs(dx) < gridSize.x+TOL);
            ASSERT(abs(dy) < gridSize.y+TOL);
            ASSERT(abs(dz) < gridSize.z+TOL);
            minx = min(minx, dx);
            maxx = max(maxx, dx);
            miny = min(miny, dy);
            maxy = max(maxy, dy);
            minz = min(minz, dz);
            maxz = max(maxz, dz);
        }
        ASSERT_EQUAL_TOL(-minx, gridSize.x, TOL);
        ASSERT_EQUAL_TOL(maxx, gridSize.x, TOL);
        ASSERT_EQUAL_TOL(-miny, gridSize.y, TOL);
        ASSERT_EQUAL_TOL(maxy, gridSize.y, TOL);
        ASSERT_EQUAL_TOL(-minz, gridSize.z, TOL);
        ASSERT_EQUAL_TOL(maxz, gridSize.z, TOL);
    }

    // Verify that interactions were identified correctly.

    vector<cl_uint> interactionCount;
    vector<mm_ushort2> interactingTiles;
    vector<cl_uint> interactionFlags;
    nb.getInteractionCount().download(interactionCount);
    int numWithInteractions = interactionCount[0];
    vector<bool> hasInteractions(numBlocks*(numBlocks+1)/2, false);
    nb.getInteractingTiles().download(interactingTiles);
    if (clcontext.getSIMDWidth() == 32)
        nb.getInteractionFlags().download(interactionFlags);
    const unsigned int atoms = clcontext.getPaddedNumAtoms();
    const unsigned int grid = OpenCLContext::TileSize;
    const unsigned int dim = clcontext.getNumAtomBlocks();
    for (int i = 0; i < numWithInteractions; i++) {
        unsigned int x = interactingTiles[i].x;
        unsigned int y = interactingTiles[i].y;
        int index = (x > y ? x+y*dim-y*(y+1)/2 : y+x*dim-x*(x+1)/2);
        hasInteractions[index] = true;

        // Make sure this tile really should have been flagged based on bounding volumes.

        mm_float4 gridSize1 = blockBoundingBoxes[x];
        mm_float4 gridSize2 = blockBoundingBoxes[y];
        mm_float4 center1 = blockCenters[x];
        mm_float4 center2 = blockCenters[y];
        float dx = center1.x-center2.x;
        float dy = center1.y-center2.y;
        float dz = center1.z-center2.z;
        if (periodic) {
            dx -= (float)(floor(0.5+dx/boxSize)*boxSize);
            dy -= (float)(floor(0.5+dy/boxSize)*boxSize);
            dz -= (float)(floor(0.5+dz/boxSize)*boxSize);
        }
        dx = max(0.0f, abs(dx)-gridSize1.x-gridSize2.x);
        dy = max(0.0f, abs(dy)-gridSize1.y-gridSize2.y);
        dz = max(0.0f, abs(dz)-gridSize1.z-gridSize2.z);
        ASSERT(sqrt(dx*dx+dy*dy+dz*dz) < cutoff+TOL);

        // Check the interaction flags.

        if (clcontext.getSIMDWidth() == 32) {
            unsigned int flags = interactionFlags[i];
            for (int atom2 = 0; atom2 < 32; atom2++) {
                if ((flags & 1) == 0) {
                    mm_float4 pos2 = clcontext.getPosq()[y*blockSize+atom2];
                    for (int atom1 = 0; atom1 < blockSize; ++atom1) {
                        mm_float4 pos1 = clcontext.getPosq()[x*blockSize+atom1];
                        float dx = pos2.x-pos1.x;
                        float dy = pos2.y-pos1.y;
                        float dz = pos2.z-pos1.z;
                        if (periodic) {
                            dx -= (float)(floor(0.5+dx/boxSize)*boxSize);
                            dy -= (float)(floor(0.5+dy/boxSize)*boxSize);
                            dz -= (float)(floor(0.5+dz/boxSize)*boxSize);
                        }
                        ASSERT(dx*dx+dy*dy+dz*dz > cutoff*cutoff);
                    }
                }
                flags >>= 1;
            }
        }
    }

    // Check the tiles that did not have interactions to make sure all atoms are beyond the cutoff.

    for (int i = 0; i < (int) hasInteractions.size(); i++) 
        if (!hasInteractions[i]) {
            unsigned int y = (unsigned int) std::floor(numBlocks+0.5-std::sqrt((numBlocks+0.5)*(numBlocks+0.5)-2*i));
            unsigned int x = (i-y*numBlocks+y*(y+1)/2);
            for (int atom1 = 0; atom1 < blockSize; ++atom1) {
                mm_float4 pos1 = clcontext.getPosq()[x*blockSize+atom1];
                for (int atom2 = 0; atom2 < blockSize; ++atom2) {
                    mm_float4 pos2 = clcontext.getPosq()[y*blockSize+atom2];
                    float dx = pos1.x-pos2.x;
                    float dy = pos1.y-pos2.y;
                    float dz = pos1.z-pos2.z;
                    if (periodic) {
                        dx -= (float)(floor(0.5+dx/boxSize)*boxSize);
                        dy -= (float)(floor(0.5+dy/boxSize)*boxSize);
                        dz -= (float)(floor(0.5+dz/boxSize)*boxSize);
                    }
                    ASSERT(dx*dx+dy*dy+dz*dz > cutoff*cutoff);
                }
            }
        }
}

void testDispersionCorrection() {
    // Create a box full of identical particles.

    int gridSize = 5;
    int numParticles = gridSize*gridSize*gridSize;
    double boxSize = gridSize*0.5;
    double cutoff = boxSize/3;
    OpenCLPlatform platform;
    System system;
    VerletIntegrator integrator(0.01);
    NonbondedForce* nonbonded = new NonbondedForce();
    vector<Vec3> positions(numParticles);
    int index = 0;
    for (int i = 0; i < gridSize; i++)
        for (int j = 0; j < gridSize; j++)
            for (int k = 0; k < gridSize; k++) {
                system.addParticle(1.0);
                nonbonded->addParticle(0, 1.1, 0.5);
                positions[index] = Vec3(i*boxSize/gridSize, j*boxSize/gridSize, k*boxSize/gridSize);
                index++;
            }
    nonbonded->setNonbondedMethod(NonbondedForce::CutoffPeriodic);
    nonbonded->setCutoffDistance(cutoff);
    system.setDefaultPeriodicBoxVectors(Vec3(boxSize, 0, 0), Vec3(0, boxSize, 0), Vec3(0, 0, boxSize));
    system.addForce(nonbonded);

    // See if the correction has the correct value.

    Context context(system, integrator, platform);
    context.setPositions(positions);
    double energy1 = context.getState(State::Energy).getPotentialEnergy();
    nonbonded->setUseDispersionCorrection(false);
    context.reinitialize();
    context.setPositions(positions);
    double energy2 = context.getState(State::Energy).getPotentialEnergy();
    double term1 = (0.5*pow(1.1, 12)/pow(cutoff, 9))/9;
    double term2 = (0.5*pow(1.1, 6)/pow(cutoff, 3))/3;
    double expected = 8*M_PI*numParticles*numParticles*(term1-term2)/(boxSize*boxSize*boxSize);
    ASSERT_EQUAL_TOL(expected, energy1-energy2, 1e-4);

    // Now modify half the particles to be different, and see if it is still correct.

    int numType2 = 0;
    for (int i = 0; i < numParticles; i += 2) {
        nonbonded->setParticleParameters(i, 0, 1, 1);
        numType2++;
    }
    int numType1 = numParticles-numType2;
    nonbonded->setUseDispersionCorrection(true);
    context.reinitialize();
    context.setPositions(positions);
    energy1 = context.getState(State::Energy).getPotentialEnergy();
    nonbonded->setUseDispersionCorrection(false);
    context.reinitialize();
    context.setPositions(positions);
    energy2 = context.getState(State::Energy).getPotentialEnergy();
    term1 = ((numType1*(numType1+1))/2)*(0.5*pow(1.1, 12)/pow(cutoff, 9))/9;
    term2 = ((numType1*(numType1+1))/2)*(0.5*pow(1.1, 6)/pow(cutoff, 3))/3;
    term1 += ((numType2*(numType2+1))/2)*(1*pow(1.0, 12)/pow(cutoff, 9))/9;
    term2 += ((numType2*(numType2+1))/2)*(1*pow(1.0, 6)/pow(cutoff, 3))/3;
    double combinedSigma = 0.5*(1+1.1);
    double combinedEpsilon = sqrt(1*0.5);
    term1 += (numType1*numType2)*(combinedEpsilon*pow(combinedSigma, 12)/pow(cutoff, 9))/9;
    term2 += (numType1*numType2)*(combinedEpsilon*pow(combinedSigma, 6)/pow(cutoff, 3))/3;
    term1 /= (numParticles*(numParticles+1))/2;
    term2 /= (numParticles*(numParticles+1))/2;
    expected = 8*M_PI*numParticles*numParticles*(term1-term2)/(boxSize*boxSize*boxSize);
    ASSERT_EQUAL_TOL(expected, energy1-energy2, 1e-4);
}

void testParallelComputation() {
    OpenCLPlatform platform;
    System system;
    const int numParticles = 200;
    for (int i = 0; i < numParticles; i++)
        system.addParticle(1.0);
    NonbondedForce* force = new NonbondedForce();
    for (int i = 0; i < numParticles; i++)
        force->addParticle(i%2-0.5, 0.5, 1.0);
    system.addForce(force);
    OpenMM_SFMT::SFMT sfmt;
    init_gen_rand(0, sfmt);
    vector<Vec3> positions(numParticles);
    for (int i = 0; i < numParticles; i++)
        positions[i] = Vec3(5*genrand_real2(sfmt), 5*genrand_real2(sfmt), 5*genrand_real2(sfmt));
    for (int i = 0; i < numParticles; ++i)
        for (int j = 0; j < i; ++j) {
            Vec3 delta = positions[i]-positions[j];
            if (delta.dot(delta) < 0.1)
                force->addException(i, j, 0, 1, 0);
        }
    VerletIntegrator integrator1(0.01);
    Context context1(system, integrator1, platform);
    context1.setPositions(positions);
    State state1 = context1.getState(State::Forces | State::Energy);
    VerletIntegrator integrator2(0.01);
    string deviceIndex = platform.getPropertyValue(context1, OpenCLPlatform::OpenCLDeviceIndex());
    map<string, string> props;
    props[OpenCLPlatform::OpenCLDeviceIndex()] = deviceIndex+","+deviceIndex;
    Context context2(system, integrator2, platform, props);
    context2.setPositions(positions);
    State state2 = context2.getState(State::Forces | State::Energy);
    ASSERT_EQUAL_TOL(state1.getPotentialEnergy(), state2.getPotentialEnergy(), 1e-5);
    for (int i = 0; i < numParticles; i++)
        ASSERT_EQUAL_VEC(state1.getForces()[i], state2.getForces()[i], 1e-5);
}

int main() {
    try {
        testCoulomb();
        testLJ();
        testExclusionsAnd14();
        testCutoff();
        testCutoff14();
        testPeriodic();
        testLargeSystem();
        testBlockInteractions(false);
        testBlockInteractions(true);
        testDispersionCorrection();
        testParallelComputation();
    }
    catch(const exception& e) {
        cout << "exception: " << e.what() << endl;
        return 1;
    }
    cout << "Done" << endl;
    return 0;
}

