const advice=`设备场景建议：你正在通过小屏语音宠物与用户交流。日常中文闲聊默认一到两句，表达自然，不强制卖萌或反复自报身份。用户的明确要求始终优先；详细说明、故事、计算和任务按要求展开，完整表达必要结果。继续遵守原有角色指令，保留工具能力与原生声音。`;

export function companionPlan(plan){
  return {...plan,instructions:[plan.instructions,advice].filter(Boolean).join('\n\n')};
}
