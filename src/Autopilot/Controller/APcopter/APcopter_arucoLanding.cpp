#include "APcopter_arucoLanding.h"

namespace kai
{

APcopter_arucoLanding::APcopter_arucoLanding()
{
	m_pAP = NULL;
	m_pArUco = NULL;
	m_pDV = NULL;

	m_bLocked = false;
	m_orientation.x = 1.0;
	m_orientation.y = 1.0;

	m_vGimbal.init();
	m_gimbalControl.input_a = m_vGimbal.x * 100;	//pitch
	m_gimbalControl.input_b = m_vGimbal.y * 100;	//roll
	m_gimbalControl.input_c = m_vGimbal.z * 100;	//yaw
	m_gimbalControl.save_position = 0;

	m_gimbalConfig.stab_pitch = 1;
	m_gimbalConfig.stab_roll = 1;
	m_gimbalConfig.stab_yaw = 1;
	m_gimbalConfig.mount_mode = 2;
}

APcopter_arucoLanding::~APcopter_arucoLanding()
{
}

bool APcopter_arucoLanding::init(void* pKiss)
{
	IF_F(!this->ActionBase::init(pKiss));
	Kiss* pK = (Kiss*) pKiss;

	pK->v("orientationX", &m_orientation.x);
	pK->v("orientationY", &m_orientation.y);

	Kiss* pG = pK->o("gimbal");
	if(!pG->empty())
	{
		pG->v("pitch", &m_vGimbal.x);
		pG->v("roll", &m_vGimbal.y);
		pG->v("yaw", &m_vGimbal.z);

		m_gimbalControl.input_a = m_vGimbal.x * 100;	//pitch
		m_gimbalControl.input_b = m_vGimbal.y * 100;	//roll
		m_gimbalControl.input_c = m_vGimbal.z * 100;	//yaw
		m_gimbalControl.save_position = 0;

		pG->v("stabPitch", &m_gimbalConfig.stab_pitch);
		pG->v("stabRoll", &m_gimbalConfig.stab_roll);
		pG->v("stabYaw", &m_gimbalConfig.stab_yaw);
		pG->v("mountMode", &m_gimbalConfig.mount_mode);
	}

	m_oTarget.init();

	pG = pK->o("tags");
	if(!pG->empty())
	{
		Kiss** pItrT = pG->getChildItr();
		LANDING_TARGET_ARUCO L;
		int i = 0;
		while (pItrT[i])
		{
			Kiss* pKs = pItrT[i++];
			L.init();
			F_ERROR_F(pKs->v("tag", &L.m_tag));
			pKs->v("angle", &L.m_angle);
			m_vTarget.push_back(L);
		}
	}

	//link
	string iName = "";
	F_INFO(pK->v("APcopter_base", &iName));
	m_pAP = (APcopter_base*) (pK->parent()->getChildInst(iName));

	F_ERROR_F(pK->v("_ArUco", &iName));
	m_pArUco = (_ObjectBase*) (pK->root()->getChildInst(iName));

	F_INFO(pK->v("_DepthVisionBase", &iName));
	m_pDV = (_DepthVisionBase*) (pK->root()->getChildInst(iName));

	return true;
}

int APcopter_arucoLanding::check(void)
{
	NULL__(m_pAP,-1);
	NULL__(m_pAP->m_pMavlink,-1);
	NULL__(m_pArUco,-1);
	_VisionBase* pV = m_pArUco->m_pVision;
	NULL__(pV,-1);

	return this->ActionBase::check();
}

void APcopter_arucoLanding::update(void)
{
	this->ActionBase::update();
	IF_(check()<0);
	if(!isActive())
	{
		m_pArUco->goSleep();
		return;
	}

	if(m_bStateChanged)
	{
		m_pArUco->wakeUp();
	}

	updateGimbal();

	_VisionBase* pV = m_pArUco->m_pVision;
	vInt2 cSize;
	vInt2 cCenter;
	vDouble2 cAngle;
	pV->info(&cSize, &cCenter, &cAngle);

	int iDet = 0;
	int iPriority = INT_MAX;
	LANDING_TARGET_ARUCO* pTarget = NULL;

	while(1)
	{
		OBJECT* pO = m_pArUco->at(iDet++);
		if(!pO)break;

		LANDING_TARGET_ARUCO* pT = NULL;
		int i;
		for(i=0; i<m_vTarget.size(); i++)
		{
			LANDING_TARGET_ARUCO* pTi = &m_vTarget[i];
			IF_CONT(pO->m_topClass != pTi->m_tag);

			pT = pTi;
			break;
		}

		IF_CONT(!pT);
		IF_CONT(i >= iPriority);

		pTarget = pT;
		iPriority = i;
		m_oTarget = *pO;
	}

	if(!pTarget)
	{
		m_bLocked = false;
		return;
	}
	m_bLocked = true;

	m_D.target_num = 0;
	m_D.frame = MAV_FRAME_BODY_NED;
	m_D.distance = 0;
	m_D.size_x = 0;
	m_D.size_y = 0;

	//Change position to angles
	m_D.angle_x = (float)((double)(m_oTarget.m_bb.x - cCenter.x) / (double)cSize.x)
							* cAngle.x * DEG_RAD * m_orientation.x;
	m_D.angle_y = (float)((double)(m_oTarget.m_bb.y - cCenter.y) / (double)cSize.y)
							* cAngle.y * DEG_RAD * m_orientation.y;

	//Use depth if available
	if(m_pDV)
	{
		if(m_D.distance < 0.0)m_D.distance = 0.0;
	}

	m_pAP->m_pMavlink->landingTarget(m_D);
}

void APcopter_arucoLanding::updateGimbal(void)
{
	m_pAP->m_pMavlink->mountControl(m_gimbalControl);
	m_pAP->m_pMavlink->mountConfigure(m_gimbalConfig);
}

bool APcopter_arucoLanding::draw(void)
{
	IF_F(!this->ActionBase::draw());
	Window* pWin = (Window*) this->m_pWindow;
	Mat* pMat = pWin->getFrame()->m();
	string msg = *this->getName() + ": ";
	IF_F(check()<0);

	_VisionBase* pV = m_pArUco->m_pVision;
	vDouble2 cAngle;
	pV->info(NULL, NULL, &cAngle);

	if(!isActive())
	{
		msg += "Inactive";
	}
	else if (m_bLocked)
	{
		circle(*pMat, Point(m_oTarget.m_bb.x, m_oTarget.m_bb.y),
				pMat->cols * pMat->rows * 0.0001, Scalar(0, 0, 255), 2);

		msg += "Target tag = " + i2str(m_oTarget.m_topClass)
				+ ", angle = ("
				+ f2str((double)m_D.angle_x) + " , "
				+ f2str((double)m_D.angle_y) + ")"
				+ ", fov = ("
				+ f2str(cAngle.x) + " , "
				+ f2str(cAngle.y) + ")"
				+ ", d=" + f2str(m_D.distance);
	}
	else
	{
		msg += "Target tag not found";
	}

	pWin->addMsg(&msg);

	return true;
}

bool APcopter_arucoLanding::cli(int& iY)
{
	IF_F(!this->ActionBase::cli(iY));
	IF_F(check()<0);

	_VisionBase* pV = m_pArUco->m_pVision;
	vDouble2 cAngle;
	pV->info(NULL, NULL, &cAngle);

	string msg;

	if(!isActive())
	{
		msg = "Inactive";
	}
	else if (m_bLocked)
	{
		msg = "Target tag = " + i2str(m_oTarget.m_topClass)
				+ ", angle = ("
				+ f2str((double)m_D.angle_x) + " , "
				+ f2str((double)m_D.angle_y) + ")"
				+ ", fov = ("
				+ f2str(cAngle.x) + " , "
				+ f2str(cAngle.y) + ")"
				+ ", d=" + f2str(m_D.distance);
	}
	else
	{
		msg = "Target tag not found";
	}

	COL_MSG;
	iY++;
	mvaddstr(iY, CLI_X_MSG, msg.c_str());

	return true;
}

}
