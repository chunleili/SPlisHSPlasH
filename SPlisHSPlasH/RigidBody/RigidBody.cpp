#include <math.h>
#include "RigidBody.h"
#include "SPlisHSPlasH/TimeManager.h"
#include <Eigen/Geometry>
#include <Eigen/Dense>

#define _USE_MATH_DEFINES

using namespace SPH;
using namespace std;

inline Matrix3r polarDecompose(Matrix3r A_pq);

RigidBody::RigidBody(FluidModel* model):
NonPressureForceBase(model)
{
    unsigned int numParticles = m_model->numActiveParticles();
    penalty_force.resize(numParticles);
    force.resize(numParticles);
    radius_vector.resize(numParticles);
    positions0.resize(numParticles);
}

void RigidBody::setStates()
{
    unsigned int numParticles = m_model->numActiveParticles();
    for(unsigned int i=0; i< numParticles ; i++)
    {
        m_model->setParticleState(i, ParticleState::RigidBody);
    }
}

RigidBody::~RigidBody()
{
}

void RigidBody::step()
{   
    static int steps = 0;
    if(steps == 0)
    {
        setStates();
        computeBarycenter();
    }
    collision_response();
    addForce();
    shapeMatching();
    animateParticles();
    steps++;
}

void RigidBody::shapeMatching()
{
    const unsigned int numParticles = m_model->numActiveParticles();
    Vector3r g(0.0, -9.8, 0.0);

    TimeManager *tm = TimeManager::getCurrent ();
	const Real dt = tm->getTimeStepSize();

    for(int i = 0; i < numParticles; i++)
    {
        Vector3r &pos = m_model->getPosition(i);
        Vector3r &pos0 = m_model->getPosition0(i);
        Vector3r f;
        Vector3r &vel = m_model->getVelocity(i);
        Real &mass = m_model->getMass(i);
        
        pos0 = pos;
        f = g + penalty_force[i];
        vel += 1.0/mass * f * dt;
        pos += vel *dt;
    }

    //compute the new(matched shape) mass center
    Vector3r c{0.0, 0.0, 0.0};
    for(int i = 0; i < numParticles; i++)
    {
        Vector3r &pos = m_model->getPosition(i);

        c += pos;
    }
    c /= numParticles;

    // compute transformation matrix and extract rotation
    Matrix3r A, A_pq;
    A.setZero();
    A_pq.setZero();
    for(int i = 0; i < numParticles; i++)
    {
        Vector3r &pos = m_model->getPosition(i);

        A_pq += (pos - c) * radius_vector[i].transpose();
    }
    // A = A_pq * A_qq;

    //polar decomposition
    Matrix3r R = polarDecompose(A_pq);

    //update vel and pos
    for(int i = 0; i < numParticles; i++)
    {
        Vector3r &pos = m_model->getPosition(i);
        Vector3r &vel = m_model->getVelocity(i);
        Vector3r &pos0 = m_model->getPosition0(i);

        pos = c + R * radius_vector[i];;
        vel = (pos - pos0) / dt; 
    }
}


inline Matrix3r polarDecompose(Matrix3r A_pq)
{
    Matrix3r R, S;
    R.setZero();
    S.setZero();
    S = (A_pq.transpose() * A_pq).cwiseSqrt();
    R = A_pq * S.inverse();
    return R;
}


void RigidBody::collision_response()
{
    float eps = 0.0; // the padding to prevent penatrating
    float k = 100.0; // stiffness of the penalty force
    //boundary for skybox (xmin, ymin, zmin, xmax, ymax, zmax)
    const  int numParticles = (int) m_model->numActiveParticles();

    for (int i = 0; i < numParticles; i++)
    {
        Vector3r &positions = m_model->getPosition(i);
        if (positions.y() < eps)
        {
            Vector3r n_dir(0.0, 1.0, 0.0);
            float phi = positions.y();
            penalty_force[i] = k * abs(phi)  * n_dir;
        }
    }
}

void RigidBody::addForce()
{
    const Vector3r g{0.0, -9.8, 0.0};
    const  int numParticles = (int) m_model->numActiveParticles();

    for (int i = 0; i < numParticles; i++)
    {
        force[i] = g + penalty_force[i];
    }
}


void RigidBody::animateParticles()
{
    const  int numParticles = (int) m_model->numActiveParticles();
    if (numParticles == 0)
		return;
    TimeManager *tm = TimeManager::getCurrent ();
	const Real h = tm->getTimeStepSize();
 
    for (int i = 0; i < numParticles; i++)
    { 
        if (m_model->getParticleState(i) == ParticleState::RigidBody)
        {
            Vector3r &pos = m_model->getPosition(i);
            Vector3r &vel = m_model->getVelocity(i);
            float &mass = m_model->getMass(i);
            vel += h * force[i]/mass;
            pos += h * vel; 
        }
    }
}

void RigidBody::computeBarycenter()
{
    const  int numParticles = (int) m_model->numActiveParticles();
    if (numParticles == 0)
		return;

    Vector3r sum_mass_pos = Vector3r(0.0, 0.0, 0.0);
    Real sum_mass = 0.0;

    #pragma omp parallel default(shared)
    {
        #pragma omp for schedule(static) nowait 
        for (int i = 0; i < numParticles; i++)
        { 
            const Vector3r &xi = m_model->getPosition(i);
            const Real mass = m_model->getMass(i);
            sum_mass += mass ;
            sum_mass_pos += mass * xi;
        }
        total_mass = sum_mass;
        barycenter = sum_mass_pos / sum_mass;
        
        //calculate  radius vector and A_qq
        Matrix3r q;
        q.setZero();
        for (int i = 0; i < numParticles; i++)
        { 
            const Vector3r &xi = m_model->getPosition(i);
            radius_vector[i] = xi - barycenter;

            q += radius_vector[i]*(radius_vector[i]).transpose();
        }
        A_qq = q.inverse();
    }
}